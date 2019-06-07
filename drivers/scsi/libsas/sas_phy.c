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
	struct asd_sas_event *ev = to_asd_sas_event(work);
	struct asd_sas_phy *phy = ev->phy;

	phy->error = 0;
	sas_deform_port(phy, 1);
}

static void sas_phye_oob_done(struct work_struct *work)
{
	struct asd_sas_event *ev = to_asd_sas_event(work);
	struct asd_sas_phy *phy = ev->phy;

	phy->error = 0;
}

static void sas_phye_oob_error(struct work_struct *work)
{
	struct asd_sas_event *ev = to_asd_sas_event(work);
	struct asd_sas_phy *phy = ev->phy;
	struct sas_ha_struct *sas_ha = phy->ha;
	struct asd_sas_port *port = phy->port;
	struct sas_internal *i =
		to_sas_internal(sas_ha->core.shost->transportt);

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
	struct asd_sas_event *ev = to_asd_sas_event(work);
	struct asd_sas_phy *phy = ev->phy;
	struct sas_ha_struct *sas_ha = phy->ha;
	struct sas_internal *i =
		to_sas_internal(sas_ha->core.shost->transportt);

	phy->error = 0;
	i->dft->lldd_control_phy(phy, PHY_FUNC_RELEASE_SPINUP_HOLD, NULL);
}

static void sas_phye_resume_timeout(struct work_struct *work)
{
	struct asd_sas_event *ev = to_asd_sas_event(work);
	struct asd_sas_phy *phy = ev->phy;

	/* phew, lldd got the phy back in the nick of time */
	if (!phy->suspended) {
		dev_info(&phy->phy->dev, "resume timeout cancelled\n");
		return;
	}

	phy->error = 0;
	phy->suspended = 0;
	sas_deform_port(phy, 1);
}


static void sas_phye_shutdown(struct work_struct *work)
{
	struct asd_sas_event *ev = to_asd_sas_event(work);
	struct asd_sas_phy *phy = ev->phy;
	struct sas_ha_struct *sas_ha = phy->ha;
	struct sas_internal *i =
		to_sas_internal(sas_ha->core.shost->transportt);

	if (phy->enabled) {
		int ret;

		phy->error = 0;
		phy->enabled = 0;
		ret = i->dft->lldd_control_phy(phy, PHY_FUNC_DISABLE, NULL);
		if (ret)
			pr_notice("lldd disable phy%d returned %d\n", phy->id,
				  ret);
	} else
		pr_notice("phy%d is not enabled, cannot shutdown\n", phy->id);
	phy->in_shutdown = 0;
}

/* ---------- Phy class registration ---------- */

int sas_register_phys(struct sas_ha_struct *sas_ha)
{
	int i;

	/* Now register the phys. */
	for (i = 0; i < sas_ha->num_phys; i++) {
		struct asd_sas_phy *phy = sas_ha->sas_phy[i];

		phy->error = 0;
		atomic_set(&phy->event_nr, 0);
		INIT_LIST_HEAD(&phy->port_phy_el);

		phy->port = NULL;
		phy->ha = sas_ha;
		spin_lock_init(&phy->frame_rcvd_lock);
		spin_lock_init(&phy->sas_prim_lock);
		phy->frame_rcvd_size = 0;

		phy->phy = sas_phy_alloc(&sas_ha->core.shost->shost_gendev, i);
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

const work_func_t sas_phy_event_fns[PHY_NUM_EVENTS] = {
	[PHYE_LOSS_OF_SIGNAL] = sas_phye_loss_of_signal,
	[PHYE_OOB_DONE] = sas_phye_oob_done,
	[PHYE_OOB_ERROR] = sas_phye_oob_error,
	[PHYE_SPINUP_HOLD] = sas_phye_spinup_hold,
	[PHYE_RESUME_TIMEOUT] = sas_phye_resume_timeout,
	[PHYE_SHUTDOWN] = sas_phye_shutdown,
};
