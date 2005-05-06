/*
 *                  QLOGIC LINUX SOFTWARE
 *
 * QLogic ISP2x00 device driver for Linux 2.6.x
 * Copyright (C) 2003-2005 QLogic Corporation
 * (www.qlogic.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 */
#include "qla_def.h"

#include <linux/vmalloc.h>
#include <scsi/scsi_transport_fc.h>

/* SYSFS attributes --------------------------------------------------------- */

static ssize_t
qla2x00_sysfs_read_fw_dump(struct kobject *kobj, char *buf, loff_t off,
    size_t count)
{
	struct scsi_qla_host *ha = to_qla_host(dev_to_shost(container_of(kobj,
	    struct device, kobj)));

	if (ha->fw_dump_reading == 0)
		return 0;
	if (off > ha->fw_dump_buffer_len)
		return 0;
	if (off + count > ha->fw_dump_buffer_len)
		count = ha->fw_dump_buffer_len - off;

	memcpy(buf, &ha->fw_dump_buffer[off], count);

	return (count);
}

static ssize_t
qla2x00_sysfs_write_fw_dump(struct kobject *kobj, char *buf, loff_t off,
    size_t count)
{
	struct scsi_qla_host *ha = to_qla_host(dev_to_shost(container_of(kobj,
	    struct device, kobj)));
	int reading;
	uint32_t dump_size;

	if (off != 0)
		return (0);

	reading = simple_strtol(buf, NULL, 10);
	switch (reading) {
	case 0:
		if (ha->fw_dump_reading == 1) {
			qla_printk(KERN_INFO, ha,
			    "Firmware dump cleared on (%ld).\n",
			    ha->host_no);

			vfree(ha->fw_dump_buffer);
			free_pages((unsigned long)ha->fw_dump,
			    ha->fw_dump_order);

			ha->fw_dump_reading = 0;
			ha->fw_dump_buffer = NULL;
			ha->fw_dump = NULL;
		}
		break;
	case 1:
		if (ha->fw_dump != NULL && !ha->fw_dump_reading) {
			ha->fw_dump_reading = 1;

			dump_size = FW_DUMP_SIZE_1M;
			if (ha->fw_memory_size < 0x20000) 
				dump_size = FW_DUMP_SIZE_128K;
			else if (ha->fw_memory_size < 0x80000) 
				dump_size = FW_DUMP_SIZE_512K;
			ha->fw_dump_buffer = (char *)vmalloc(dump_size);
			if (ha->fw_dump_buffer == NULL) {
				qla_printk(KERN_WARNING, ha,
				    "Unable to allocate memory for firmware "
				    "dump buffer (%d).\n", dump_size);

				ha->fw_dump_reading = 0;
				return (count);
			}
			qla_printk(KERN_INFO, ha,
			    "Firmware dump ready for read on (%ld).\n",
			    ha->host_no);
			memset(ha->fw_dump_buffer, 0, dump_size);
			if (IS_QLA2100(ha) || IS_QLA2200(ha))
 				qla2100_ascii_fw_dump(ha);
 			else
 				qla2300_ascii_fw_dump(ha);
			ha->fw_dump_buffer_len = strlen(ha->fw_dump_buffer);
		}
		break;
	}
	return (count);
}

static struct bin_attribute sysfs_fw_dump_attr = {
	.attr = {
		.name = "fw_dump",
		.mode = S_IRUSR | S_IWUSR,
		.owner = THIS_MODULE,
	},
	.size = 0,
	.read = qla2x00_sysfs_read_fw_dump,
	.write = qla2x00_sysfs_write_fw_dump,
};

static ssize_t
qla2x00_sysfs_read_nvram(struct kobject *kobj, char *buf, loff_t off,
    size_t count)
{
	struct scsi_qla_host *ha = to_qla_host(dev_to_shost(container_of(kobj,
	    struct device, kobj)));
	uint16_t	*witer;
	unsigned long	flags;
	uint16_t	cnt;

	if (!capable(CAP_SYS_ADMIN) || off != 0 || count != sizeof(nvram_t))
		return 0;

	/* Read NVRAM. */
	spin_lock_irqsave(&ha->hardware_lock, flags);
	qla2x00_lock_nvram_access(ha);
 	witer = (uint16_t *)buf;
 	for (cnt = 0; cnt < count / 2; cnt++) {
		*witer = cpu_to_le16(qla2x00_get_nvram_word(ha,
		    cnt+ha->nvram_base));
		witer++;
 	}
	qla2x00_unlock_nvram_access(ha);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	return (count);
}

static ssize_t
qla2x00_sysfs_write_nvram(struct kobject *kobj, char *buf, loff_t off,
    size_t count)
{
	struct scsi_qla_host *ha = to_qla_host(dev_to_shost(container_of(kobj,
	    struct device, kobj)));
	uint8_t		*iter;
	uint16_t	*witer;
	unsigned long	flags;
	uint16_t	cnt;
	uint8_t		chksum;

	if (!capable(CAP_SYS_ADMIN) || off != 0 || count != sizeof(nvram_t))
		return 0;

	/* Checksum NVRAM. */
	iter = (uint8_t *)buf;
	chksum = 0;
	for (cnt = 0; cnt < count - 1; cnt++)
		chksum += *iter++;
	chksum = ~chksum + 1;
	*iter = chksum;

	/* Write NVRAM. */
	spin_lock_irqsave(&ha->hardware_lock, flags);
	qla2x00_lock_nvram_access(ha);
	qla2x00_release_nvram_protection(ha);
 	witer = (uint16_t *)buf;
	for (cnt = 0; cnt < count / 2; cnt++) {
		qla2x00_write_nvram_word(ha, cnt+ha->nvram_base,
		    cpu_to_le16(*witer));
		witer++;
	}
	qla2x00_unlock_nvram_access(ha);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	return (count);
}

static struct bin_attribute sysfs_nvram_attr = {
	.attr = {
		.name = "nvram",
		.mode = S_IRUSR | S_IWUSR,
		.owner = THIS_MODULE,
	},
	.size = sizeof(nvram_t),
	.read = qla2x00_sysfs_read_nvram,
	.write = qla2x00_sysfs_write_nvram,
};

void
qla2x00_alloc_sysfs_attr(scsi_qla_host_t *ha)
{
	struct Scsi_Host *host = ha->host;

	sysfs_create_bin_file(&host->shost_gendev.kobj, &sysfs_fw_dump_attr);
	sysfs_create_bin_file(&host->shost_gendev.kobj, &sysfs_nvram_attr);
}

void
qla2x00_free_sysfs_attr(scsi_qla_host_t *ha)
{
	struct Scsi_Host *host = ha->host;

	sysfs_remove_bin_file(&host->shost_gendev.kobj, &sysfs_fw_dump_attr);
	sysfs_remove_bin_file(&host->shost_gendev.kobj, &sysfs_nvram_attr);
}

/* Host attributes. */

static void
qla2x00_get_host_port_id(struct Scsi_Host *shost)
{
	scsi_qla_host_t *ha = to_qla_host(shost);

	fc_host_port_id(shost) = ha->d_id.b.domain << 16 |
	    ha->d_id.b.area << 8 | ha->d_id.b.al_pa;
}

static void
qla2x00_get_starget_node_name(struct scsi_target *starget)
{
	struct Scsi_Host *host = dev_to_shost(starget->dev.parent);
	scsi_qla_host_t *ha = to_qla_host(host);
	fc_port_t *fcport;
	uint64_t node_name = 0;

	list_for_each_entry(fcport, &ha->fcports, list) {
		if (starget->id == fcport->os_target_id) {
			node_name = *(uint64_t *)fcport->node_name;
			break;
		}
	}

	fc_starget_node_name(starget) = be64_to_cpu(node_name);
}

static void
qla2x00_get_starget_port_name(struct scsi_target *starget)
{
	struct Scsi_Host *host = dev_to_shost(starget->dev.parent);
	scsi_qla_host_t *ha = to_qla_host(host);
	fc_port_t *fcport;
	uint64_t port_name = 0;

	list_for_each_entry(fcport, &ha->fcports, list) {
		if (starget->id == fcport->os_target_id) {
			port_name = *(uint64_t *)fcport->port_name;
			break;
		}
	}

	fc_starget_port_name(starget) = be64_to_cpu(port_name);
}

static void
qla2x00_get_starget_port_id(struct scsi_target *starget)
{
	struct Scsi_Host *host = dev_to_shost(starget->dev.parent);
	scsi_qla_host_t *ha = to_qla_host(host);
	fc_port_t *fcport;
	uint32_t port_id = ~0U;

	list_for_each_entry(fcport, &ha->fcports, list) {
		if (starget->id == fcport->os_target_id) {
			port_id = fcport->d_id.b.domain << 16 |
			    fcport->d_id.b.area << 8 | fcport->d_id.b.al_pa;
			break;
		}
	}

	fc_starget_port_id(starget) = port_id;
}

static void
qla2x00_get_rport_loss_tmo(struct fc_rport *rport)
{
	struct Scsi_Host *host = rport_to_shost(rport);
	scsi_qla_host_t *ha = to_qla_host(host);

	rport->dev_loss_tmo = ha->port_down_retry_count + 5;
}

static void
qla2x00_set_rport_loss_tmo(struct fc_rport *rport, uint32_t timeout)
{
	struct Scsi_Host *host = rport_to_shost(rport);
	scsi_qla_host_t *ha = to_qla_host(host);

	if (timeout)
		ha->port_down_retry_count = timeout;
	else
		ha->port_down_retry_count = 1;

	rport->dev_loss_tmo = ha->port_down_retry_count + 5;
}

struct fc_function_template qla2xxx_transport_functions = {

	.show_host_node_name = 1,
	.show_host_port_name = 1,
	.get_host_port_id = qla2x00_get_host_port_id,
	.show_host_port_id = 1,

	.dd_fcrport_size = sizeof(struct fc_port *),

	.get_starget_node_name = qla2x00_get_starget_node_name,
	.show_starget_node_name = 1,
	.get_starget_port_name = qla2x00_get_starget_port_name,
	.show_starget_port_name = 1,
	.get_starget_port_id  = qla2x00_get_starget_port_id,
	.show_starget_port_id = 1,

	.get_rport_dev_loss_tmo = qla2x00_get_rport_loss_tmo,
	.set_rport_dev_loss_tmo = qla2x00_set_rport_loss_tmo,
	.show_rport_dev_loss_tmo = 1,

};

void
qla2x00_init_host_attr(scsi_qla_host_t *ha)
{
	fc_host_node_name(ha->host) =
	    be64_to_cpu(*(uint64_t *)ha->init_cb->node_name);
	fc_host_port_name(ha->host) =
	    be64_to_cpu(*(uint64_t *)ha->init_cb->port_name);
}
