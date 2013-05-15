/*
 * Serial Attached SCSI (SAS) class internal header file
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

#ifndef _SAS_INTERNAL_H_
#define _SAS_INTERNAL_H_

#include <scsi/scsi.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_transport_sas.h>
#include <scsi/libsas.h>
#include <scsi/sas_ata.h>

#define sas_printk(fmt, ...) printk(KERN_NOTICE "sas: " fmt, ## __VA_ARGS__)

#define SAS_DPRINTK(fmt, ...) printk(KERN_DEBUG "sas: " fmt, ## __VA_ARGS__)

#define TO_SAS_TASK(_scsi_cmd)  ((void *)(_scsi_cmd)->host_scribble)
#define ASSIGN_SAS_TASK(_sc, _t) do { (_sc)->host_scribble = (void *) _t; } while (0)

struct sas_phy_data {
	/* let reset be performed in sas_queue_work() context */
	struct sas_phy *phy;
	struct mutex event_lock;
	int hard_reset;
	int reset_result;
	struct sas_work reset_work;
	int enable;
	int enable_result;
	struct sas_work enable_work;
};

void sas_scsi_recover_host(struct Scsi_Host *shost);

int sas_show_class(enum sas_class class, char *buf);
int sas_show_proto(enum sas_protocol proto, char *buf);
int sas_show_linkrate(enum sas_linkrate linkrate, char *buf);
int sas_show_oob_mode(enum sas_oob_mode oob_mode, char *buf);

int  sas_register_phys(struct sas_ha_struct *sas_ha);
void sas_unregister_phys(struct sas_ha_struct *sas_ha);

int  sas_register_ports(struct sas_ha_struct *sas_ha);
void sas_unregister_ports(struct sas_ha_struct *sas_ha);

enum blk_eh_timer_return sas_scsi_timed_out(struct scsi_cmnd *);

int  sas_init_queue(struct sas_ha_struct *sas_ha);
int  sas_init_events(struct sas_ha_struct *sas_ha);
void sas_shutdown_queue(struct sas_ha_struct *sas_ha);
void sas_disable_revalidation(struct sas_ha_struct *ha);
void sas_enable_revalidation(struct sas_ha_struct *ha);
void __sas_drain_work(struct sas_ha_struct *ha);

void sas_deform_port(struct asd_sas_phy *phy, int gone);

void sas_porte_bytes_dmaed(struct work_struct *work);
void sas_porte_broadcast_rcvd(struct work_struct *work);
void sas_porte_link_reset_err(struct work_struct *work);
void sas_porte_timer_event(struct work_struct *work);
void sas_porte_hard_reset(struct work_struct *work);
void sas_queue_work(struct sas_ha_struct *ha, struct sas_work *sw);

int sas_notify_lldd_dev_found(struct domain_device *);
void sas_notify_lldd_dev_gone(struct domain_device *);

int sas_smp_phy_control(struct domain_device *dev, int phy_id,
			enum phy_func phy_func, struct sas_phy_linkrates *);
int sas_smp_get_phy_events(struct sas_phy *phy);

void sas_notify_phy_event(struct asd_sas_phy *phy, enum phy_event event);
void sas_device_set_phy(struct domain_device *dev, struct sas_port *port);
struct domain_device *sas_find_dev_by_rphy(struct sas_rphy *rphy);
struct domain_device *sas_ex_to_ata(struct domain_device *ex_dev, int phy_id);
int sas_ex_phy_discover(struct domain_device *dev, int single);
int sas_get_report_phy_sata(struct domain_device *dev, int phy_id,
			    struct smp_resp *rps_resp);
int sas_try_ata_reset(struct asd_sas_phy *phy);
void sas_hae_reset(struct work_struct *work);

void sas_free_device(struct kref *kref);

#ifdef CONFIG_SCSI_SAS_HOST_SMP
extern int sas_smp_host_handler(struct Scsi_Host *shost, struct request *req,
				struct request *rsp);
#else
static inline int sas_smp_host_handler(struct Scsi_Host *shost,
				       struct request *req,
				       struct request *rsp)
{
	shost_printk(KERN_ERR, shost,
		"Cannot send SMP to a sas host (not enabled in CONFIG)\n");
	return -EINVAL;
}
#endif

static inline void sas_fail_probe(struct domain_device *dev, const char *func, int err)
{
	SAS_DPRINTK("%s: for %s device %16llx returned %d\n",
		    func, dev->parent ? "exp-attached" :
					    "direct-attached",
		    SAS_ADDR(dev->sas_addr), err);
	sas_unregister_dev(dev->port, dev);
}

static inline void sas_fill_in_rphy(struct domain_device *dev,
				    struct sas_rphy *rphy)
{
	rphy->identify.sas_address = SAS_ADDR(dev->sas_addr);
	rphy->identify.initiator_port_protocols = dev->iproto;
	rphy->identify.target_port_protocols = dev->tproto;
	switch (dev->dev_type) {
	case SAS_SATA_DEV:
		/* FIXME: need sata device type */
	case SAS_END_DEVICE:
	case SAS_SATA_PENDING:
		rphy->identify.device_type = SAS_END_DEVICE;
		break;
	case SAS_EDGE_EXPANDER_DEVICE:
		rphy->identify.device_type = SAS_EDGE_EXPANDER_DEVICE;
		break;
	case SAS_FANOUT_EXPANDER_DEVICE:
		rphy->identify.device_type = SAS_FANOUT_EXPANDER_DEVICE;
		break;
	default:
		rphy->identify.device_type = SAS_PHY_UNUSED;
		break;
	}
}

static inline void sas_phy_set_target(struct asd_sas_phy *p, struct domain_device *dev)
{
	struct sas_phy *phy = p->phy;

	if (dev) {
		if (dev_is_sata(dev))
			phy->identify.device_type = SAS_END_DEVICE;
		else
			phy->identify.device_type = dev->dev_type;
		phy->identify.target_port_protocols = dev->tproto;
	} else {
		phy->identify.device_type = SAS_PHY_UNUSED;
		phy->identify.target_port_protocols = 0;
	}
}

static inline void sas_add_parent_port(struct domain_device *dev, int phy_id)
{
	struct expander_device *ex = &dev->ex_dev;
	struct ex_phy *ex_phy = &ex->ex_phy[phy_id];

	if (!ex->parent_port) {
		ex->parent_port = sas_port_alloc(&dev->rphy->dev, phy_id);
		/* FIXME: error handling */
		BUG_ON(!ex->parent_port);
		BUG_ON(sas_port_add(ex->parent_port));
		sas_port_mark_backlink(ex->parent_port);
	}
	sas_port_add_phy(ex->parent_port, ex_phy->phy);
}

static inline struct domain_device *sas_alloc_device(void)
{
	struct domain_device *dev = kzalloc(sizeof(*dev), GFP_KERNEL);

	if (dev) {
		INIT_LIST_HEAD(&dev->siblings);
		INIT_LIST_HEAD(&dev->dev_list_node);
		INIT_LIST_HEAD(&dev->disco_list_node);
		kref_init(&dev->kref);
		spin_lock_init(&dev->done_lock);
	}
	return dev;
}

static inline void sas_put_device(struct domain_device *dev)
{
	kref_put(&dev->kref, sas_free_device);
}

#endif /* _SAS_INTERNAL_H_ */
