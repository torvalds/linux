/*
 * Copyright (c) 2005-2009 Brocade Communications Systems, Inc.
 * All rights reserved
 * www.brocade.com
 *
 * Linux driver for Brocade Fibre Channel Host Bus Adapter.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License (GPL) Version 2 as
 * published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

/**
 *  bfa_attr.c Linux driver configuration interface module.
 */

#include "bfad_drv.h"
#include "bfad_im.h"
#include "bfad_trcmod.h"
#include "bfad_attr.h"

/**
 *  FC_transport_template FC transport template
 */

/**
 * FC transport template entry, get SCSI target port ID.
 */
void
bfad_im_get_starget_port_id(struct scsi_target *starget)
{
	struct Scsi_Host *shost;
	struct bfad_im_port_s *im_port;
	struct bfad_s         *bfad;
	struct bfad_itnim_s   *itnim = NULL;
	u32        fc_id = -1;
	unsigned long   flags;

	shost = bfad_os_starget_to_shost(starget);
	im_port = (struct bfad_im_port_s *) shost->hostdata[0];
	bfad = im_port->bfad;
	spin_lock_irqsave(&bfad->bfad_lock, flags);

	itnim = bfad_os_get_itnim(im_port, starget->id);
	if (itnim)
		fc_id = bfa_fcs_itnim_get_fcid(&itnim->fcs_itnim);

	fc_starget_port_id(starget) = fc_id;
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
}

/**
 * FC transport template entry, get SCSI target nwwn.
 */
void
bfad_im_get_starget_node_name(struct scsi_target *starget)
{
	struct Scsi_Host *shost;
	struct bfad_im_port_s *im_port;
	struct bfad_s         *bfad;
	struct bfad_itnim_s   *itnim = NULL;
	u64             node_name = 0;
	unsigned long   flags;

	shost = bfad_os_starget_to_shost(starget);
	im_port = (struct bfad_im_port_s *) shost->hostdata[0];
	bfad = im_port->bfad;
	spin_lock_irqsave(&bfad->bfad_lock, flags);

	itnim = bfad_os_get_itnim(im_port, starget->id);
	if (itnim)
		node_name = bfa_fcs_itnim_get_nwwn(&itnim->fcs_itnim);

	fc_starget_node_name(starget) = bfa_os_htonll(node_name);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
}

/**
 * FC transport template entry, get SCSI target pwwn.
 */
void
bfad_im_get_starget_port_name(struct scsi_target *starget)
{
	struct Scsi_Host *shost;
	struct bfad_im_port_s *im_port;
	struct bfad_s         *bfad;
	struct bfad_itnim_s   *itnim = NULL;
	u64             port_name = 0;
	unsigned long   flags;

	shost = bfad_os_starget_to_shost(starget);
	im_port = (struct bfad_im_port_s *) shost->hostdata[0];
	bfad = im_port->bfad;
	spin_lock_irqsave(&bfad->bfad_lock, flags);

	itnim = bfad_os_get_itnim(im_port, starget->id);
	if (itnim)
		port_name = bfa_fcs_itnim_get_pwwn(&itnim->fcs_itnim);

	fc_starget_port_name(starget) = bfa_os_htonll(port_name);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
}

/**
 * FC transport template entry, get SCSI host port ID.
 */
void
bfad_im_get_host_port_id(struct Scsi_Host *shost)
{
	struct bfad_im_port_s *im_port =
			(struct bfad_im_port_s *) shost->hostdata[0];
	struct bfad_port_s    *port = im_port->port;

	fc_host_port_id(shost) =
			bfa_os_hton3b(bfa_fcs_port_get_fcid(port->fcs_port));
}





struct Scsi_Host *
bfad_os_starget_to_shost(struct scsi_target *starget)
{
	return dev_to_shost(starget->dev.parent);
}

/**
 * FC transport template entry, get SCSI host port type.
 */
static void
bfad_im_get_host_port_type(struct Scsi_Host *shost)
{
	struct bfad_im_port_s *im_port =
			(struct bfad_im_port_s *) shost->hostdata[0];
	struct bfad_s         *bfad = im_port->bfad;
	struct bfa_pport_attr_s attr;

	bfa_pport_get_attr(&bfad->bfa, &attr);

	switch (attr.port_type) {
	case BFA_PPORT_TYPE_NPORT:
		fc_host_port_type(shost) = FC_PORTTYPE_NPORT;
		break;
	case BFA_PPORT_TYPE_NLPORT:
		fc_host_port_type(shost) = FC_PORTTYPE_NLPORT;
		break;
	case BFA_PPORT_TYPE_P2P:
		fc_host_port_type(shost) = FC_PORTTYPE_PTP;
		break;
	case BFA_PPORT_TYPE_LPORT:
		fc_host_port_type(shost) = FC_PORTTYPE_LPORT;
		break;
	default:
		fc_host_port_type(shost) = FC_PORTTYPE_UNKNOWN;
		break;
	}
}

/**
 * FC transport template entry, get SCSI host port state.
 */
static void
bfad_im_get_host_port_state(struct Scsi_Host *shost)
{
	struct bfad_im_port_s *im_port =
			(struct bfad_im_port_s *) shost->hostdata[0];
	struct bfad_s         *bfad = im_port->bfad;
	struct bfa_pport_attr_s attr;

	bfa_pport_get_attr(&bfad->bfa, &attr);

	switch (attr.port_state) {
	case BFA_PPORT_ST_LINKDOWN:
		fc_host_port_state(shost) = FC_PORTSTATE_LINKDOWN;
		break;
	case BFA_PPORT_ST_LINKUP:
		fc_host_port_state(shost) = FC_PORTSTATE_ONLINE;
		break;
	case BFA_PPORT_ST_UNINIT:
	case BFA_PPORT_ST_ENABLING_QWAIT:
	case BFA_PPORT_ST_ENABLING:
	case BFA_PPORT_ST_DISABLING_QWAIT:
	case BFA_PPORT_ST_DISABLING:
	case BFA_PPORT_ST_DISABLED:
	case BFA_PPORT_ST_STOPPED:
	case BFA_PPORT_ST_IOCDOWN:
	default:
		fc_host_port_state(shost) = FC_PORTSTATE_UNKNOWN;
		break;
	}
}

/**
 * FC transport template entry, get SCSI host active fc4s.
 */
static void
bfad_im_get_host_active_fc4s(struct Scsi_Host *shost)
{
	struct bfad_im_port_s *im_port =
			(struct bfad_im_port_s *) shost->hostdata[0];
	struct bfad_port_s    *port = im_port->port;

	memset(fc_host_active_fc4s(shost), 0,
	       sizeof(fc_host_active_fc4s(shost)));

	if (port->supported_fc4s &
		(BFA_PORT_ROLE_FCP_IM | BFA_PORT_ROLE_FCP_TM))
		fc_host_active_fc4s(shost)[2] = 1;

	if (port->supported_fc4s & BFA_PORT_ROLE_FCP_IPFC)
		fc_host_active_fc4s(shost)[3] = 0x20;

	fc_host_active_fc4s(shost)[7] = 1;
}

/**
 * FC transport template entry, get SCSI host link speed.
 */
static void
bfad_im_get_host_speed(struct Scsi_Host *shost)
{
	struct bfad_im_port_s *im_port =
			(struct bfad_im_port_s *) shost->hostdata[0];
	struct bfad_s         *bfad = im_port->bfad;
	struct bfa_pport_attr_s attr;
	unsigned long   flags;

	spin_lock_irqsave(shost->host_lock, flags);
	bfa_pport_get_attr(&bfad->bfa, &attr);
	switch (attr.speed) {
	case BFA_PPORT_SPEED_8GBPS:
		fc_host_speed(shost) = FC_PORTSPEED_8GBIT;
		break;
	case BFA_PPORT_SPEED_4GBPS:
		fc_host_speed(shost) = FC_PORTSPEED_4GBIT;
		break;
	case BFA_PPORT_SPEED_2GBPS:
		fc_host_speed(shost) = FC_PORTSPEED_2GBIT;
		break;
	case BFA_PPORT_SPEED_1GBPS:
		fc_host_speed(shost) = FC_PORTSPEED_1GBIT;
		break;
	default:
		fc_host_speed(shost) = FC_PORTSPEED_UNKNOWN;
		break;
	}
	spin_unlock_irqrestore(shost->host_lock, flags);
}

/**
 * FC transport template entry, get SCSI host port type.
 */
static void
bfad_im_get_host_fabric_name(struct Scsi_Host *shost)
{
	struct bfad_im_port_s *im_port =
			(struct bfad_im_port_s *) shost->hostdata[0];
	struct bfad_port_s    *port = im_port->port;
	wwn_t           fabric_nwwn = 0;

	fabric_nwwn = bfa_fcs_port_get_fabric_name(port->fcs_port);

	fc_host_fabric_name(shost) = bfa_os_htonll(fabric_nwwn);

}

/**
 * FC transport template entry, get BFAD statistics.
 */
static struct fc_host_statistics *
bfad_im_get_stats(struct Scsi_Host *shost)
{
	struct bfad_im_port_s *im_port =
			(struct bfad_im_port_s *) shost->hostdata[0];
	struct bfad_s         *bfad = im_port->bfad;
	struct bfad_hal_comp fcomp;
	struct fc_host_statistics *hstats;
	bfa_status_t    rc;
	unsigned long   flags;

	hstats = &bfad->link_stats;
	init_completion(&fcomp.comp);
	spin_lock_irqsave(&bfad->bfad_lock, flags);
	memset(hstats, 0, sizeof(struct fc_host_statistics));
	rc = bfa_pport_get_stats(&bfad->bfa,
				     (union bfa_pport_stats_u *) hstats,
				     bfad_hcb_comp, &fcomp);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	if (rc != BFA_STATUS_OK)
		return NULL;

	wait_for_completion(&fcomp.comp);

	return hstats;
}

/**
 * FC transport template entry, reset BFAD statistics.
 */
static void
bfad_im_reset_stats(struct Scsi_Host *shost)
{
	struct bfad_im_port_s *im_port =
			(struct bfad_im_port_s *) shost->hostdata[0];
	struct bfad_s         *bfad = im_port->bfad;
	struct bfad_hal_comp fcomp;
	unsigned long   flags;
	bfa_status_t    rc;

	init_completion(&fcomp.comp);
	spin_lock_irqsave(&bfad->bfad_lock, flags);
	rc = bfa_pport_clear_stats(&bfad->bfa, bfad_hcb_comp, &fcomp);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);

	if (rc != BFA_STATUS_OK)
		return;

	wait_for_completion(&fcomp.comp);

	return;
}

/**
 * FC transport template entry, get rport loss timeout.
 */
static void
bfad_im_get_rport_loss_tmo(struct fc_rport *rport)
{
	struct bfad_itnim_data_s *itnim_data = rport->dd_data;
	struct bfad_itnim_s   *itnim = itnim_data->itnim;
	struct bfad_s         *bfad = itnim->im->bfad;
	unsigned long   flags;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	rport->dev_loss_tmo = bfa_fcpim_path_tov_get(&bfad->bfa);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
}

/**
 * FC transport template entry, set rport loss timeout.
 */
static void
bfad_im_set_rport_loss_tmo(struct fc_rport *rport, u32 timeout)
{
	struct bfad_itnim_data_s *itnim_data = rport->dd_data;
	struct bfad_itnim_s   *itnim = itnim_data->itnim;
	struct bfad_s         *bfad = itnim->im->bfad;
	unsigned long   flags;

	if (timeout > 0) {
		spin_lock_irqsave(&bfad->bfad_lock, flags);
		bfa_fcpim_path_tov_set(&bfad->bfa, timeout);
		rport->dev_loss_tmo = bfa_fcpim_path_tov_get(&bfad->bfa);
		spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	}

}

struct fc_function_template bfad_im_fc_function_template = {

	/* Target dynamic attributes */
	.get_starget_port_id = bfad_im_get_starget_port_id,
	.show_starget_port_id = 1,
	.get_starget_node_name = bfad_im_get_starget_node_name,
	.show_starget_node_name = 1,
	.get_starget_port_name = bfad_im_get_starget_port_name,
	.show_starget_port_name = 1,

	/* Host dynamic attribute */
	.get_host_port_id = bfad_im_get_host_port_id,
	.show_host_port_id = 1,

	/* Host fixed attributes */
	.show_host_node_name = 1,
	.show_host_port_name = 1,
	.show_host_supported_classes = 1,
	.show_host_supported_fc4s = 1,
	.show_host_supported_speeds = 1,
	.show_host_maxframe_size = 1,

	/* More host dynamic attributes */
	.show_host_port_type = 1,
	.get_host_port_type = bfad_im_get_host_port_type,
	.show_host_port_state = 1,
	.get_host_port_state = bfad_im_get_host_port_state,
	.show_host_active_fc4s = 1,
	.get_host_active_fc4s = bfad_im_get_host_active_fc4s,
	.show_host_speed = 1,
	.get_host_speed = bfad_im_get_host_speed,
	.show_host_fabric_name = 1,
	.get_host_fabric_name = bfad_im_get_host_fabric_name,

	.show_host_symbolic_name = 1,

	/* Statistics */
	.get_fc_host_stats = bfad_im_get_stats,
	.reset_fc_host_stats = bfad_im_reset_stats,

	/* Allocation length for host specific data */
	.dd_fcrport_size = sizeof(struct bfad_itnim_data_s *),

	/* Remote port fixed attributes */
	.show_rport_maxframe_size = 1,
	.show_rport_supported_classes = 1,
	.show_rport_dev_loss_tmo = 1,
	.get_rport_dev_loss_tmo = bfad_im_get_rport_loss_tmo,
	.set_rport_dev_loss_tmo = bfad_im_set_rport_loss_tmo,
};

/**
 *  Scsi_Host_attrs SCSI host attributes
 */
static ssize_t
bfad_im_serial_num_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct bfad_im_port_s *im_port =
			(struct bfad_im_port_s *) shost->hostdata[0];
	struct bfad_s         *bfad = im_port->bfad;
	struct bfa_ioc_attr_s  ioc_attr;

	memset(&ioc_attr, 0, sizeof(ioc_attr));
	bfa_get_attr(&bfad->bfa, &ioc_attr);
	return snprintf(buf, PAGE_SIZE, "%s\n",
			ioc_attr.adapter_attr.serial_num);
}

static ssize_t
bfad_im_model_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct bfad_im_port_s *im_port =
			(struct bfad_im_port_s *) shost->hostdata[0];
	struct bfad_s         *bfad = im_port->bfad;
	struct bfa_ioc_attr_s  ioc_attr;

	memset(&ioc_attr, 0, sizeof(ioc_attr));
	bfa_get_attr(&bfad->bfa, &ioc_attr);
	return snprintf(buf, PAGE_SIZE, "%s\n", ioc_attr.adapter_attr.model);
}

static ssize_t
bfad_im_model_desc_show(struct device *dev, struct device_attribute *attr,
				 char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct bfad_im_port_s *im_port =
			(struct bfad_im_port_s *) shost->hostdata[0];
	struct bfad_s         *bfad = im_port->bfad;
	struct bfa_ioc_attr_s  ioc_attr;

	memset(&ioc_attr, 0, sizeof(ioc_attr));
	bfa_get_attr(&bfad->bfa, &ioc_attr);
	return snprintf(buf, PAGE_SIZE, "%s\n",
			ioc_attr.adapter_attr.model_descr);
}

static ssize_t
bfad_im_node_name_show(struct device *dev, struct device_attribute *attr,
				 char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct bfad_im_port_s *im_port =
			(struct bfad_im_port_s *) shost->hostdata[0];
	struct bfad_port_s    *port = im_port->port;
	u64        nwwn;

	nwwn = bfa_fcs_port_get_nwwn(port->fcs_port);
	return snprintf(buf, PAGE_SIZE, "0x%llx\n", bfa_os_htonll(nwwn));
}

static ssize_t
bfad_im_symbolic_name_show(struct device *dev, struct device_attribute *attr,
				 char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct bfad_im_port_s *im_port =
			(struct bfad_im_port_s *) shost->hostdata[0];
	struct bfad_s         *bfad = im_port->bfad;
	struct bfa_ioc_attr_s  ioc_attr;

	memset(&ioc_attr, 0, sizeof(ioc_attr));
	bfa_get_attr(&bfad->bfa, &ioc_attr);

	return snprintf(buf, PAGE_SIZE, "Brocade %s FV%s DV%s\n",
			ioc_attr.adapter_attr.model,
			ioc_attr.adapter_attr.fw_ver, BFAD_DRIVER_VERSION);
}

static ssize_t
bfad_im_hw_version_show(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct bfad_im_port_s *im_port =
			(struct bfad_im_port_s *) shost->hostdata[0];
	struct bfad_s         *bfad = im_port->bfad;
	struct bfa_ioc_attr_s  ioc_attr;

	memset(&ioc_attr, 0, sizeof(ioc_attr));
	bfa_get_attr(&bfad->bfa, &ioc_attr);
	return snprintf(buf, PAGE_SIZE, "%s\n", ioc_attr.adapter_attr.hw_ver);
}

static ssize_t
bfad_im_drv_version_show(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%s\n", BFAD_DRIVER_VERSION);
}

static ssize_t
bfad_im_optionrom_version_show(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct bfad_im_port_s *im_port =
			(struct bfad_im_port_s *) shost->hostdata[0];
	struct bfad_s         *bfad = im_port->bfad;
	struct bfa_ioc_attr_s  ioc_attr;

	memset(&ioc_attr, 0, sizeof(ioc_attr));
	bfa_get_attr(&bfad->bfa, &ioc_attr);
	return snprintf(buf, PAGE_SIZE, "%s\n",
			ioc_attr.adapter_attr.optrom_ver);
}

static ssize_t
bfad_im_fw_version_show(struct device *dev, struct device_attribute *attr,
				 char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct bfad_im_port_s *im_port =
			(struct bfad_im_port_s *) shost->hostdata[0];
	struct bfad_s         *bfad = im_port->bfad;
	struct bfa_ioc_attr_s  ioc_attr;

	memset(&ioc_attr, 0, sizeof(ioc_attr));
	bfa_get_attr(&bfad->bfa, &ioc_attr);
	return snprintf(buf, PAGE_SIZE, "%s\n", ioc_attr.adapter_attr.fw_ver);
}

static ssize_t
bfad_im_num_of_ports_show(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct bfad_im_port_s *im_port =
			(struct bfad_im_port_s *) shost->hostdata[0];
	struct bfad_s         *bfad = im_port->bfad;
	struct bfa_ioc_attr_s  ioc_attr;

	memset(&ioc_attr, 0, sizeof(ioc_attr));
	bfa_get_attr(&bfad->bfa, &ioc_attr);
	return snprintf(buf, PAGE_SIZE, "%d\n", ioc_attr.adapter_attr.nports);
}

static ssize_t
bfad_im_drv_name_show(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%s\n", BFAD_DRIVER_NAME);
}

static ssize_t
bfad_im_num_of_discovered_ports_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct bfad_im_port_s *im_port =
			(struct bfad_im_port_s *) shost->hostdata[0];
	struct bfad_port_s    *port = im_port->port;
	struct bfad_s         *bfad = im_port->bfad;
	int        nrports = 2048;
	wwn_t          *rports = NULL;
	unsigned long   flags;

	rports = kzalloc(sizeof(wwn_t) * nrports , GFP_ATOMIC);
	if (rports == NULL)
		return -ENOMEM;

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	bfa_fcs_port_get_rports(port->fcs_port, rports, &nrports);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	kfree(rports);

	return snprintf(buf, PAGE_SIZE, "%d\n", nrports);
}

static          DEVICE_ATTR(serial_number, S_IRUGO,
				bfad_im_serial_num_show, NULL);
static          DEVICE_ATTR(model, S_IRUGO, bfad_im_model_show, NULL);
static          DEVICE_ATTR(model_description, S_IRUGO,
				bfad_im_model_desc_show, NULL);
static          DEVICE_ATTR(node_name, S_IRUGO, bfad_im_node_name_show, NULL);
static          DEVICE_ATTR(symbolic_name, S_IRUGO,
				bfad_im_symbolic_name_show, NULL);
static          DEVICE_ATTR(hardware_version, S_IRUGO,
				bfad_im_hw_version_show, NULL);
static          DEVICE_ATTR(driver_version, S_IRUGO,
				bfad_im_drv_version_show, NULL);
static          DEVICE_ATTR(option_rom_version, S_IRUGO,
				bfad_im_optionrom_version_show, NULL);
static          DEVICE_ATTR(firmware_version, S_IRUGO,
				bfad_im_fw_version_show, NULL);
static          DEVICE_ATTR(number_of_ports, S_IRUGO,
				bfad_im_num_of_ports_show, NULL);
static          DEVICE_ATTR(driver_name, S_IRUGO, bfad_im_drv_name_show, NULL);
static          DEVICE_ATTR(number_of_discovered_ports, S_IRUGO,
				bfad_im_num_of_discovered_ports_show, NULL);

struct device_attribute *bfad_im_host_attrs[] = {
	&dev_attr_serial_number,
	&dev_attr_model,
	&dev_attr_model_description,
	&dev_attr_node_name,
	&dev_attr_symbolic_name,
	&dev_attr_hardware_version,
	&dev_attr_driver_version,
	&dev_attr_option_rom_version,
	&dev_attr_firmware_version,
	&dev_attr_number_of_ports,
	&dev_attr_driver_name,
	&dev_attr_number_of_discovered_ports,
	NULL,
};

struct device_attribute *bfad_im_vport_attrs[] = {
    &dev_attr_serial_number,
    &dev_attr_model,
    &dev_attr_model_description,
    &dev_attr_node_name,
    &dev_attr_symbolic_name,
    &dev_attr_hardware_version,
    &dev_attr_driver_version,
    &dev_attr_option_rom_version,
    &dev_attr_firmware_version,
    &dev_attr_number_of_ports,
    &dev_attr_driver_name,
    &dev_attr_number_of_discovered_ports,
    NULL,
};


