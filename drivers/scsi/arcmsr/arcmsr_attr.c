/*
*******************************************************************************
**        O.S   : Linux
**   FILE NAME  : arcmsr_attr.c
**        BY    : Erich Chen
**   Description: attributes exported to sysfs and device host
*******************************************************************************
** Copyright (C) 2002 - 2005, Areca Technology Corporation All rights reserved
**
**     Web site: www.areca.com.tw
**       E-mail: erich@areca.com.tw
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License version 2 as
** published by the Free Software Foundation.
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
*******************************************************************************
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. The name of the author may not be used to endorse or promote products
**    derived from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
** IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES(INCLUDING,BUT
** NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION)HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE)ARISING IN ANY WAY OUT OF THE USE OF
** THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*******************************************************************************
** For history of changes, see Documentation/scsi/ChangeLog.arcmsr
**     Firmware Specification, see Documentation/scsi/arcmsr_spec.txt
*******************************************************************************
*/
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/pci.h>

#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_transport.h>
#include "arcmsr.h"

struct class_device_attribute *arcmsr_host_attrs[];

static ssize_t
arcmsr_sysfs_iop_message_read(struct kobject *kobj, char *buf, loff_t off,
    size_t count)
{
	struct class_device *cdev = container_of(kobj,struct class_device,kobj);
	struct Scsi_Host *host = class_to_shost(cdev);
	struct AdapterControlBlock *acb = (struct AdapterControlBlock *) host->hostdata;
	struct MessageUnit __iomem *reg = acb->pmu;
	uint8_t *pQbuffer,*ptmpQbuffer;
	int32_t allxfer_len = 0;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	/* do message unit read. */
	ptmpQbuffer = (uint8_t *)buf;
	while ((acb->rqbuf_firstindex != acb->rqbuf_lastindex)
		&& (allxfer_len < 1031)) {
		pQbuffer = &acb->rqbuffer[acb->rqbuf_firstindex];
		memcpy(ptmpQbuffer, pQbuffer, 1);
		acb->rqbuf_firstindex++;
		acb->rqbuf_firstindex %= ARCMSR_MAX_QBUFFER;
		ptmpQbuffer++;
		allxfer_len++;
	}
	if (acb->acb_flags & ACB_F_IOPDATA_OVERFLOW) {
		struct QBUFFER __iomem * prbuffer = (struct QBUFFER __iomem *)
					&reg->message_rbuffer;
		uint8_t __iomem * iop_data = (uint8_t __iomem *)prbuffer->data;
		int32_t iop_len;

		acb->acb_flags &= ~ACB_F_IOPDATA_OVERFLOW;
		iop_len = readl(&prbuffer->data_len);
		while (iop_len > 0) {
			acb->rqbuffer[acb->rqbuf_lastindex] = readb(iop_data);
			acb->rqbuf_lastindex++;
			acb->rqbuf_lastindex %= ARCMSR_MAX_QBUFFER;
			iop_data++;
			iop_len--;
		}
		writel(ARCMSR_INBOUND_DRIVER_DATA_READ_OK,
				&reg->inbound_doorbell);
	}
	return (allxfer_len);
}

static ssize_t
arcmsr_sysfs_iop_message_write(struct kobject *kobj, char *buf, loff_t off,
    size_t count)
{
	struct class_device *cdev = container_of(kobj,struct class_device,kobj);
	struct Scsi_Host *host = class_to_shost(cdev);
	struct AdapterControlBlock *acb = (struct AdapterControlBlock *) host->hostdata;
	int32_t my_empty_len, user_len, wqbuf_firstindex, wqbuf_lastindex;
	uint8_t *pQbuffer, *ptmpuserbuffer;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;
	if (count > 1032)
		return -EINVAL;
	/* do message unit write. */
	ptmpuserbuffer = (uint8_t *)buf;
	user_len = (int32_t)count;
	wqbuf_lastindex = acb->wqbuf_lastindex;
	wqbuf_firstindex = acb->wqbuf_firstindex;
	if (wqbuf_lastindex != wqbuf_firstindex) {
		arcmsr_post_Qbuffer(acb);
		return 0;	/*need retry*/
	} else {
		my_empty_len = (wqbuf_firstindex-wqbuf_lastindex - 1)
				&(ARCMSR_MAX_QBUFFER - 1);
		if (my_empty_len >= user_len) {
			while (user_len > 0) {
				pQbuffer =
				&acb->wqbuffer[acb->wqbuf_lastindex];
				memcpy(pQbuffer, ptmpuserbuffer, 1);
				acb->wqbuf_lastindex++;
				acb->wqbuf_lastindex %= ARCMSR_MAX_QBUFFER;
				ptmpuserbuffer++;
				user_len--;
			}
			if (acb->acb_flags & ACB_F_MESSAGE_WQBUFFER_CLEARED) {
				acb->acb_flags &=
					~ACB_F_MESSAGE_WQBUFFER_CLEARED;
				arcmsr_post_Qbuffer(acb);
			}
			return count;
		} else {
			return 0;	/*need retry*/
		}
	}
}

static ssize_t
arcmsr_sysfs_iop_message_clear(struct kobject *kobj, char *buf, loff_t off,
    size_t count)
{
	struct class_device *cdev = container_of(kobj,struct class_device,kobj);
	struct Scsi_Host *host = class_to_shost(cdev);
	struct AdapterControlBlock *acb = (struct AdapterControlBlock *) host->hostdata;
	struct MessageUnit __iomem *reg = acb->pmu;
	uint8_t *pQbuffer;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	if (acb->acb_flags & ACB_F_IOPDATA_OVERFLOW) {
		acb->acb_flags &= ~ACB_F_IOPDATA_OVERFLOW;
		writel(ARCMSR_INBOUND_DRIVER_DATA_READ_OK
				, &reg->inbound_doorbell);
	}
	acb->acb_flags |=
		(ACB_F_MESSAGE_WQBUFFER_CLEARED
		| ACB_F_MESSAGE_RQBUFFER_CLEARED
		| ACB_F_MESSAGE_WQBUFFER_READED);
	acb->rqbuf_firstindex = 0;
	acb->rqbuf_lastindex = 0;
	acb->wqbuf_firstindex = 0;
	acb->wqbuf_lastindex = 0;
	pQbuffer = acb->rqbuffer;
	memset(pQbuffer, 0, sizeof (struct QBUFFER));
	pQbuffer = acb->wqbuffer;
	memset(pQbuffer, 0, sizeof (struct QBUFFER));
	return 1;
}

static struct bin_attribute arcmsr_sysfs_message_read_attr = {
	.attr = {
		.name = "mu_read",
		.mode = S_IRUSR ,
		.owner = THIS_MODULE,
	},
	.size = 1032,
	.read = arcmsr_sysfs_iop_message_read,
};

static struct bin_attribute arcmsr_sysfs_message_write_attr = {
	.attr = {
		.name = "mu_write",
		.mode = S_IWUSR,
		.owner = THIS_MODULE,
	},
	.size = 1032,
	.write = arcmsr_sysfs_iop_message_write,
};

static struct bin_attribute arcmsr_sysfs_message_clear_attr = {
	.attr = {
		.name = "mu_clear",
		.mode = S_IWUSR,
		.owner = THIS_MODULE,
	},
	.size = 1,
	.write = arcmsr_sysfs_iop_message_clear,
};

int arcmsr_alloc_sysfs_attr(struct AdapterControlBlock *acb)
{
	struct Scsi_Host *host = acb->host;
	int error;

	error = sysfs_create_bin_file(&host->shost_classdev.kobj,
				&arcmsr_sysfs_message_read_attr);
	if (error) {
		printk(KERN_ERR "arcmsr: alloc sysfs mu_read failed\n");
		goto error_bin_file_message_read;
	}
	error = sysfs_create_bin_file(&host->shost_classdev.kobj,
				&arcmsr_sysfs_message_write_attr);
	if (error) {
		printk(KERN_ERR "arcmsr: alloc sysfs mu_write failed\n");
		goto error_bin_file_message_write;
	}
	error = sysfs_create_bin_file(&host->shost_classdev.kobj,
				&arcmsr_sysfs_message_clear_attr);
	if (error) {
		printk(KERN_ERR "arcmsr: alloc sysfs mu_clear failed\n");
		goto error_bin_file_message_clear;
	}
	return 0;
error_bin_file_message_clear:
	sysfs_remove_bin_file(&host->shost_classdev.kobj,
				&arcmsr_sysfs_message_write_attr);
error_bin_file_message_write:
	sysfs_remove_bin_file(&host->shost_classdev.kobj,
				&arcmsr_sysfs_message_read_attr);
error_bin_file_message_read:
	return error;
}

void
arcmsr_free_sysfs_attr(struct AdapterControlBlock *acb) {
	struct Scsi_Host *host = acb->host;

	sysfs_remove_bin_file(&host->shost_classdev.kobj,
				&arcmsr_sysfs_message_clear_attr);
	sysfs_remove_bin_file(&host->shost_classdev.kobj,
				&arcmsr_sysfs_message_write_attr);
	sysfs_remove_bin_file(&host->shost_classdev.kobj,
				&arcmsr_sysfs_message_read_attr);
}


static ssize_t
arcmsr_attr_host_driver_version(struct class_device *cdev, char *buf) {
	return snprintf(buf, PAGE_SIZE,
			"%s\n",
			ARCMSR_DRIVER_VERSION);
}

static ssize_t
arcmsr_attr_host_driver_posted_cmd(struct class_device *cdev, char *buf) {
	struct Scsi_Host *host = class_to_shost(cdev);
	struct AdapterControlBlock *acb = (struct AdapterControlBlock *) host->hostdata;
	return snprintf(buf, PAGE_SIZE,
			"%4d\n",
			atomic_read(&acb->ccboutstandingcount));
}

static ssize_t
arcmsr_attr_host_driver_reset(struct class_device *cdev, char *buf) {
	struct Scsi_Host *host = class_to_shost(cdev);
	struct AdapterControlBlock *acb = (struct AdapterControlBlock *) host->hostdata;
	return snprintf(buf, PAGE_SIZE,
			"%4d\n",
			acb->num_resets);
}

static ssize_t
arcmsr_attr_host_driver_abort(struct class_device *cdev, char *buf) {
	struct Scsi_Host *host = class_to_shost(cdev);
	struct AdapterControlBlock *acb = (struct AdapterControlBlock *) host->hostdata;
	return snprintf(buf, PAGE_SIZE,
			"%4d\n",
			acb->num_aborts);
}

static ssize_t
arcmsr_attr_host_fw_model(struct class_device *cdev, char *buf) {
    struct Scsi_Host *host = class_to_shost(cdev);
	struct AdapterControlBlock *acb = (struct AdapterControlBlock *) host->hostdata;
	return snprintf(buf, PAGE_SIZE,
			"%s\n",
			acb->firm_model);
}

static ssize_t
arcmsr_attr_host_fw_version(struct class_device *cdev, char *buf) {
	struct Scsi_Host *host = class_to_shost(cdev);
	struct AdapterControlBlock *acb = (struct AdapterControlBlock *) host->hostdata;

	return snprintf(buf, PAGE_SIZE,
			"%s\n",
			acb->firm_version);
}

static ssize_t
arcmsr_attr_host_fw_request_len(struct class_device *cdev, char *buf) {
	struct Scsi_Host *host = class_to_shost(cdev);
	struct AdapterControlBlock *acb = (struct AdapterControlBlock *) host->hostdata;

	return snprintf(buf, PAGE_SIZE,
			"%4d\n",
			acb->firm_request_len);
}

static ssize_t
arcmsr_attr_host_fw_numbers_queue(struct class_device *cdev, char *buf) {
	struct Scsi_Host *host = class_to_shost(cdev);
	struct AdapterControlBlock *acb = (struct AdapterControlBlock *) host->hostdata;

	return snprintf(buf, PAGE_SIZE,
			"%4d\n",
			acb->firm_numbers_queue);
}

static ssize_t
arcmsr_attr_host_fw_sdram_size(struct class_device *cdev, char *buf) {
	struct Scsi_Host *host = class_to_shost(cdev);
	struct AdapterControlBlock *acb = (struct AdapterControlBlock *) host->hostdata;

	return snprintf(buf, PAGE_SIZE,
			"%4d\n",
			acb->firm_sdram_size);
}

static ssize_t
arcmsr_attr_host_fw_hd_channels(struct class_device *cdev, char *buf) {
	struct Scsi_Host *host = class_to_shost(cdev);
	struct AdapterControlBlock *acb = (struct AdapterControlBlock *) host->hostdata;

	return snprintf(buf, PAGE_SIZE,
			"%4d\n",
			acb->firm_hd_channels);
}

static CLASS_DEVICE_ATTR(host_driver_version, S_IRUGO, arcmsr_attr_host_driver_version, NULL);
static CLASS_DEVICE_ATTR(host_driver_posted_cmd, S_IRUGO, arcmsr_attr_host_driver_posted_cmd, NULL);
static CLASS_DEVICE_ATTR(host_driver_reset, S_IRUGO, arcmsr_attr_host_driver_reset, NULL);
static CLASS_DEVICE_ATTR(host_driver_abort, S_IRUGO, arcmsr_attr_host_driver_abort, NULL);
static CLASS_DEVICE_ATTR(host_fw_model, S_IRUGO, arcmsr_attr_host_fw_model, NULL);
static CLASS_DEVICE_ATTR(host_fw_version, S_IRUGO, arcmsr_attr_host_fw_version, NULL);
static CLASS_DEVICE_ATTR(host_fw_request_len, S_IRUGO, arcmsr_attr_host_fw_request_len, NULL);
static CLASS_DEVICE_ATTR(host_fw_numbers_queue, S_IRUGO, arcmsr_attr_host_fw_numbers_queue, NULL);
static CLASS_DEVICE_ATTR(host_fw_sdram_size, S_IRUGO, arcmsr_attr_host_fw_sdram_size, NULL);
static CLASS_DEVICE_ATTR(host_fw_hd_channels, S_IRUGO, arcmsr_attr_host_fw_hd_channels, NULL);

struct class_device_attribute *arcmsr_host_attrs[] = {
	&class_device_attr_host_driver_version,
	&class_device_attr_host_driver_posted_cmd,
	&class_device_attr_host_driver_reset,
	&class_device_attr_host_driver_abort,
	&class_device_attr_host_fw_model,
	&class_device_attr_host_fw_version,
	&class_device_attr_host_fw_request_len,
	&class_device_attr_host_fw_numbers_queue,
	&class_device_attr_host_fw_sdram_size,
	&class_device_attr_host_fw_hd_channels,
	NULL,
};
