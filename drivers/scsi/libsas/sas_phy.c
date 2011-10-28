/*
 * Serial Attached SCSI (SAS) Phy class
 *
 * Copyright (C) 2005 Adaptec, Inc.  All rights reserved.
 * Copyright (C) 2005 Luben Tuikov <luben_tuikov@adaptec.com>
 *
 * This file is licensed under GPLv2.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include "sas_internal.h"
#include <scsi/scsi_host.h>
#include <scsi/scsi_transport.h>
#include <scsi/scsi_transport_sas.h>
#include "../scsi_sas_internal.h"

/* ---------- Phy events ---------- */

static void sas_phye_loss_of_signal(struct work_struct *work)
{
	struct asd_sas_event *ev =
		container_of(work, struct asd_sas_event, work);
	struct asd_sas_phy *phy = ev->phy;

	sas_begin_event(PHYE_LOSS_OF_SIGNAL, &phy->ha->event_lock,
			&phy->phy_events_pending);
	phy->error = 0;
	sas_deform_port(phy, 1);
}

static void sas_phye_oob_done(struct work_struct *work)
{
	struct asd_sas_event *ev =
		container_of(work, struct asd_sas_event, work);
	struct asd_sas_phy *phy = ev->phy;

	sas_begin_event(PHYE_OOB_DONE, &phy->ha->event_lock,
			&phy->phy_events_pending);
	phy->error = 0;
}

static void sas_phye_oob_error(struct work_struct *work)
{
	struct asd_sas_event *ev =
		container_of(work, struct asd_sas_event, work);
	struct asd_sas_phy *phy = ev->phy;
	struct sas_ha_struct *sas_ha = phy->ha;
	struct asd_sas_port *port = phy->port;
	struct sas_internal *i =
		to_sas_internal(sas_ha->core.shost->transportt);

	sas_begin_event(PHYE_OOB_ERROR, &phy->ha->event_lock,
			&phy->phy_events_pending);

	sas_deform_port(phy, 1);

	if (!port && phy->enabled && i->dft->lldd_control_phy) {
		phy->error++;
		switch (phy->error) {
		case 1:
		case 2:
			i->dft->lldd_control_phy(phy, PHY_FUNC_HARD_RESET,
						 NULL);
			break;
		case 3:
		default:
			phy->error = 0;
			phy->enabled = 0;
			i->dft->lldd_control_phy(phy, PHY_FUNC_DISABLE, NULL);
			break;
		}
	}
}

static void sas_phye_spinup_hold(struct work_struct *work)
{
	struct asd_sas_event *ev =
		container_of(work, struct asd_sas_event, work);
	struct asd_sas_phy *phy = ev->phy;
	struct sas_ha_struct *sas_ha = phy->ha;
	struct sas_internal *i =
		to_sas_internal(sas_ha->core.shost->transportt);

	sas_begin_event(PHYE_SPINUP_HOLD, &phy->ha->event_lock,
			&phy->phy_events_pending);

	phy->error = 0;
	i->dft->lldd_control_phy(phy, PHY_FUNC_RELEASE_SPINUP_HOLD, NULL);
}

/* ---------- Phy class registration ---------- */

int sas_register_phys(struct sas_ha_struct *sas_ha)
{
	int i;

	static const work_func_t sas_phy_event_fns[PHY_NUM_EVENTS] = {
		[PHYE_LOSS_OF_SIGNAL] = sas_phye_loss_of_signal,
		[PHYE_OOB_DONE] = sas_phye_oob_done,
		[PHYE_OOB_ERROR] = sas_phye_oob_error,
		[PHYE_SPINUP_HOLD] = sas_phye_spinup_hold,
	};

	static const work_func_t sas_port_event_fns[PORT_NUM_EVENTS] = {
		[PORTE_BYTES_DMAED] = sas_porte_bytes_dmaed,
		[PORTE_BROADCAST_RCVD] = sas_porte_broadcast_rcvd,
		[PORTE_LINK_RESET_ERR] = sas_porte_link_reset_err,
		[PORTE_TIMER_EVENT] = sas_porte_timer_event,
		[PORTE_HARD_RESET] = sas_porte_hard_reset,
	};

	/* Now register the phys. */
	for (i = 0; i < sas_ha->num_phys; i++) {
		int k;
		struct asd_sas_phy *phy = sas_ha->sas_phy[i];

		phy->error = 0;
		INIT_LIST_HEAD(&phy->port_phy_el);
		for (k = 0; k < PORT_NUM_EVENTS; k++) {
			INIT_WORK(&phy->port_events[k].work,
				  sas_port_event_fns[k]);
			phy->port_events[k].phy = phy;
		}

		for (k = 0; k < PHY_NUM_EVENTS; k++) {
			INIT_WORK(&phy->phy_events[k].work,
				  sas_phy_event_fns[k]);
			phy->phy_events[k].phy = phy;
		}

		phy->port = NULL;
		phy->ha = sas_ha;
		spin_lock_init(&phy->frame_rcvd_lock);
		spin_lock_init(&phy->sas_prim_lock);
		phy->frame_rcvd_size = 0;

		phy->phy = sas_phy_alloc(&sas_ha->core.shost->shost_gendev,
					 i);
		if (!phy->phy)
			return -ENOMEM;

		phy->phy->identify.initiator_port_protocols =
			phy->iproto;
		phy->phy->identify.target_port_protocols = phy->tproto;
		phy->phy->identify.sas_address = SAS_ADDR(sas_ha->sas_addr);
		phy->phy->identify.phy_identifier = i;
		phy->phy->minimum_linkrate_hw = SAS_LINK_RATE_UNKNOWN;
		phy->phy->maximum_linkrate_hw = SAS_LINK_RATE_UNKNOWN;
		phy->phy->minimum_linkrate = SAS_LINK_RATE_UNKNOWN;
		phy->phy->maximum_linkrate = SAS_LINK_RATE_UNKNOWN;
		phy->phy->negotiated_linkrate = SAS_LINK_RATE_UNKNOWN;

		sas_phy_add(phy->phy);
	}

	return 0;
}
