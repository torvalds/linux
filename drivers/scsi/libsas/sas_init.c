/*
 * Serial Attached SCSI (SAS) Transport Layer initialization
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/spinlock.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_transport.h>
#include <scsi/scsi_transport_sas.h>

#include "sas_internal.h"

#include "../scsi_sas_internal.h"

kmem_cache_t *sas_task_cache;

/*------------ SAS addr hash -----------*/
void sas_hash_addr(u8 *hashed, const u8 *sas_addr)
{
        const u32 poly = 0x00DB2777;
        u32     r = 0;
        int     i;

        for (i = 0; i < 8; i++) {
                int b;
                for (b = 7; b >= 0; b--) {
                        r <<= 1;
                        if ((1 << b) & sas_addr[i]) {
                                if (!(r & 0x01000000))
                                        r ^= poly;
                        } else if (r & 0x01000000)
                                r ^= poly;
                }
        }

        hashed[0] = (r >> 16) & 0xFF;
        hashed[1] = (r >> 8) & 0xFF ;
        hashed[2] = r & 0xFF;
}


/* ---------- HA events ---------- */

void sas_hae_reset(struct work_struct *work)
{
	struct sas_ha_event *ev =
		container_of(work, struct sas_ha_event, work);
	struct sas_ha_struct *ha = ev->ha;

	sas_begin_event(HAE_RESET, &ha->event_lock,
			&ha->pending);
}

int sas_register_ha(struct sas_ha_struct *sas_ha)
{
	int error = 0;

	spin_lock_init(&sas_ha->phy_port_lock);
	sas_hash_addr(sas_ha->hashed_sas_addr, sas_ha->sas_addr);

	if (sas_ha->lldd_queue_size == 0)
		sas_ha->lldd_queue_size = 1;
	else if (sas_ha->lldd_queue_size == -1)
		sas_ha->lldd_queue_size = 128; /* Sanity */

	error = sas_register_phys(sas_ha);
	if (error) {
		printk(KERN_NOTICE "couldn't register sas phys:%d\n", error);
		return error;
	}

	error = sas_register_ports(sas_ha);
	if (error) {
		printk(KERN_NOTICE "couldn't register sas ports:%d\n", error);
		goto Undo_phys;
	}

	error = sas_init_events(sas_ha);
	if (error) {
		printk(KERN_NOTICE "couldn't start event thread:%d\n", error);
		goto Undo_ports;
	}

	if (sas_ha->lldd_max_execute_num > 1) {
		error = sas_init_queue(sas_ha);
		if (error) {
			printk(KERN_NOTICE "couldn't start queue thread:%d, "
			       "running in direct mode\n", error);
			sas_ha->lldd_max_execute_num = 1;
		}
	}

	return 0;

Undo_ports:
	sas_unregister_ports(sas_ha);
Undo_phys:

	return error;
}

int sas_unregister_ha(struct sas_ha_struct *sas_ha)
{
	if (sas_ha->lldd_max_execute_num > 1) {
		sas_shutdown_queue(sas_ha);
	}

	sas_unregister_ports(sas_ha);

	return 0;
}

static int sas_get_linkerrors(struct sas_phy *phy)
{
	if (scsi_is_sas_phy_local(phy))
		/* FIXME: we have no local phy stats
		 * gathering at this time */
		return -EINVAL;

	return sas_smp_get_phy_events(phy);
}

static int sas_phy_reset(struct sas_phy *phy, int hard_reset)
{
	int ret;
	enum phy_func reset_type;

	if (hard_reset)
		reset_type = PHY_FUNC_HARD_RESET;
	else
		reset_type = PHY_FUNC_LINK_RESET;

	if (scsi_is_sas_phy_local(phy)) {
		struct Scsi_Host *shost = dev_to_shost(phy->dev.parent);
		struct sas_ha_struct *sas_ha = SHOST_TO_SAS_HA(shost);
		struct asd_sas_phy *asd_phy = sas_ha->sas_phy[phy->number];
		struct sas_internal *i =
			to_sas_internal(sas_ha->core.shost->transportt);

		ret = i->dft->lldd_control_phy(asd_phy, reset_type, NULL);
	} else {
		struct sas_rphy *rphy = dev_to_rphy(phy->dev.parent);
		struct domain_device *ddev = sas_find_dev_by_rphy(rphy);
		ret = sas_smp_phy_control(ddev, phy->number, reset_type, NULL);
	}
	return ret;
}

static int sas_set_phy_speed(struct sas_phy *phy,
			     struct sas_phy_linkrates *rates)
{
	int ret;

	if ((rates->minimum_linkrate &&
	     rates->minimum_linkrate > phy->maximum_linkrate) ||
	    (rates->maximum_linkrate &&
	     rates->maximum_linkrate < phy->minimum_linkrate))
		return -EINVAL;

	if (rates->minimum_linkrate &&
	    rates->minimum_linkrate < phy->minimum_linkrate_hw)
		rates->minimum_linkrate = phy->minimum_linkrate_hw;

	if (rates->maximum_linkrate &&
	    rates->maximum_linkrate > phy->maximum_linkrate_hw)
		rates->maximum_linkrate = phy->maximum_linkrate_hw;

	if (scsi_is_sas_phy_local(phy)) {
		struct Scsi_Host *shost = dev_to_shost(phy->dev.parent);
		struct sas_ha_struct *sas_ha = SHOST_TO_SAS_HA(shost);
		struct asd_sas_phy *asd_phy = sas_ha->sas_phy[phy->number];
		struct sas_internal *i =
			to_sas_internal(sas_ha->core.shost->transportt);

		ret = i->dft->lldd_control_phy(asd_phy, PHY_FUNC_SET_LINK_RATE,
					       rates);
	} else {
		struct sas_rphy *rphy = dev_to_rphy(phy->dev.parent);
		struct domain_device *ddev = sas_find_dev_by_rphy(rphy);
		ret = sas_smp_phy_control(ddev, phy->number,
					  PHY_FUNC_LINK_RESET, rates);

	}

	return ret;
}

static struct sas_function_template sft = {
	.phy_reset = sas_phy_reset,
	.set_phy_speed = sas_set_phy_speed,
	.get_linkerrors = sas_get_linkerrors,
};

struct scsi_transport_template *
sas_domain_attach_transport(struct sas_domain_function_template *dft)
{
	struct scsi_transport_template *stt = sas_attach_transport(&sft);
	struct sas_internal *i;

	if (!stt)
		return stt;

	i = to_sas_internal(stt);
	i->dft = dft;
	stt->create_work_queue = 1;
	stt->eh_timed_out = sas_scsi_timed_out;
	stt->eh_strategy_handler = sas_scsi_recover_host;

	return stt;
}
EXPORT_SYMBOL_GPL(sas_domain_attach_transport);


void sas_domain_release_transport(struct scsi_transport_template *stt)
{
	sas_release_transport(stt);
}
EXPORT_SYMBOL_GPL(sas_domain_release_transport);

/* ---------- SAS Class register/unregister ---------- */

static int __init sas_class_init(void)
{
	sas_task_cache = kmem_cache_create("sas_task", sizeof(struct sas_task),
					   0, SLAB_HWCACHE_ALIGN, NULL, NULL);
	if (!sas_task_cache)
		return -ENOMEM;

	return 0;
}

static void __exit sas_class_exit(void)
{
	kmem_cache_destroy(sas_task_cache);
}

MODULE_AUTHOR("Luben Tuikov <luben_tuikov@adaptec.com>");
MODULE_DESCRIPTION("SAS Transport Layer");
MODULE_LICENSE("GPL v2");

module_init(sas_class_init);
module_exit(sas_class_exit);

EXPORT_SYMBOL_GPL(sas_register_ha);
EXPORT_SYMBOL_GPL(sas_unregister_ha);
