/*
 * PMC-Sierra 8001/8081/8088/8089 SAS/SATA based host adapters driver
 *
 * Copyright (c) 2008-2009 USI Co., Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 *
 */
#include <linux/firmware.h>
#include <linux/slab.h>
#include "pm8001_sas.h"
#include "pm8001_ctl.h"

/* scsi host attributes */

/**
 * pm8001_ctl_mpi_interface_rev_show - MPI interface revision number
 * @cdev: pointer to embedded class device
 * @buf: the buffer returned
 *
 * A sysfs 'read-only' shost attribute.
 */
static ssize_t pm8001_ctl_mpi_interface_rev_show(struct device *cdev,
	struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct sas_ha_struct *sha = SHOST_TO_SAS_HA(shost);
	struct pm8001_hba_info *pm8001_ha = sha->lldd_ha;

	if (pm8001_ha->chip_id == chip_8001) {
		return snprintf(buf, PAGE_SIZE, "%d\n",
			pm8001_ha->main_cfg_tbl.pm8001_tbl.interface_rev);
	} else {
		return snprintf(buf, PAGE_SIZE, "%d\n",
			pm8001_ha->main_cfg_tbl.pm80xx_tbl.interface_rev);
	}
}
static
DEVICE_ATTR(interface_rev, S_IRUGO, pm8001_ctl_mpi_interface_rev_show, NULL);

/**
 * pm8001_ctl_fw_version_show - firmware version
 * @cdev: pointer to embedded class device
 * @buf: the buffer returned
 *
 * A sysfs 'read-only' shost attribute.
 */
static ssize_t pm8001_ctl_fw_version_show(struct device *cdev,
	struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct sas_ha_struct *sha = SHOST_TO_SAS_HA(shost);
	struct pm8001_hba_info *pm8001_ha = sha->lldd_ha;

	if (pm8001_ha->chip_id == chip_8001) {
		return snprintf(buf, PAGE_SIZE, "%02x.%02x.%02x.%02x\n",
		(u8)(pm8001_ha->main_cfg_tbl.pm8001_tbl.firmware_rev >> 24),
		(u8)(pm8001_ha->main_cfg_tbl.pm8001_tbl.firmware_rev >> 16),
		(u8)(pm8001_ha->main_cfg_tbl.pm8001_tbl.firmware_rev >> 8),
		(u8)(pm8001_ha->main_cfg_tbl.pm8001_tbl.firmware_rev));
	} else {
		return snprintf(buf, PAGE_SIZE, "%02x.%02x.%02x.%02x\n",
		(u8)(pm8001_ha->main_cfg_tbl.pm80xx_tbl.firmware_rev >> 24),
		(u8)(pm8001_ha->main_cfg_tbl.pm80xx_tbl.firmware_rev >> 16),
		(u8)(pm8001_ha->main_cfg_tbl.pm80xx_tbl.firmware_rev >> 8),
		(u8)(pm8001_ha->main_cfg_tbl.pm80xx_tbl.firmware_rev));
	}
}
static DEVICE_ATTR(fw_version, S_IRUGO, pm8001_ctl_fw_version_show, NULL);
/**
 * pm8001_ctl_max_out_io_show - max outstanding io supported
 * @cdev: pointer to embedded class device
 * @buf: the buffer returned
 *
 * A sysfs 'read-only' shost attribute.
 */
static ssize_t pm8001_ctl_max_out_io_show(struct device *cdev,
	struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct sas_ha_struct *sha = SHOST_TO_SAS_HA(shost);
	struct pm8001_hba_info *pm8001_ha = sha->lldd_ha;

	if (pm8001_ha->chip_id == chip_8001) {
		return snprintf(buf, PAGE_SIZE, "%d\n",
			pm8001_ha->main_cfg_tbl.pm8001_tbl.max_out_io);
	} else {
		return snprintf(buf, PAGE_SIZE, "%d\n",
			pm8001_ha->main_cfg_tbl.pm80xx_tbl.max_out_io);
	}
}
static DEVICE_ATTR(max_out_io, S_IRUGO, pm8001_ctl_max_out_io_show, NULL);
/**
 * pm8001_ctl_max_devices_show - max devices support
 * @cdev: pointer to embedded class device
 * @buf: the buffer returned
 *
 * A sysfs 'read-only' shost attribute.
 */
static ssize_t pm8001_ctl_max_devices_show(struct device *cdev,
	struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct sas_ha_struct *sha = SHOST_TO_SAS_HA(shost);
	struct pm8001_hba_info *pm8001_ha = sha->lldd_ha;

	if (pm8001_ha->chip_id == chip_8001) {
		return snprintf(buf, PAGE_SIZE, "%04d\n",
			(u16)(pm8001_ha->main_cfg_tbl.pm8001_tbl.max_sgl >> 16)
			);
	} else {
		return snprintf(buf, PAGE_SIZE, "%04d\n",
			(u16)(pm8001_ha->main_cfg_tbl.pm80xx_tbl.max_sgl >> 16)
			);
	}
}
static DEVICE_ATTR(max_devices, S_IRUGO, pm8001_ctl_max_devices_show, NULL);
/**
 * pm8001_ctl_max_sg_list_show - max sg list supported iff not 0.0 for no
 * hardware limitation
 * @cdev: pointer to embedded class device
 * @buf: the buffer returned
 *
 * A sysfs 'read-only' shost attribute.
 */
static ssize_t pm8001_ctl_max_sg_list_show(struct device *cdev,
	struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct sas_ha_struct *sha = SHOST_TO_SAS_HA(shost);
	struct pm8001_hba_info *pm8001_ha = sha->lldd_ha;

	if (pm8001_ha->chip_id == chip_8001) {
		return snprintf(buf, PAGE_SIZE, "%04d\n",
			pm8001_ha->main_cfg_tbl.pm8001_tbl.max_sgl & 0x0000FFFF
			);
	} else {
		return snprintf(buf, PAGE_SIZE, "%04d\n",
			pm8001_ha->main_cfg_tbl.pm80xx_tbl.max_sgl & 0x0000FFFF
			);
	}
}
static DEVICE_ATTR(max_sg_list, S_IRUGO, pm8001_ctl_max_sg_list_show, NULL);

#define SAS_1_0 0x1
#define SAS_1_1 0x2
#define SAS_2_0 0x4

static ssize_t
show_sas_spec_support_status(unsigned int mode, char *buf)
{
	ssize_t len = 0;

	if (mode & SAS_1_1)
		len = sprintf(buf, "%s", "SAS1.1");
	if (mode & SAS_2_0)
		len += sprintf(buf + len, "%s%s", len ? ", " : "", "SAS2.0");
	len += sprintf(buf + len, "\n");

	return len;
}

/**
 * pm8001_ctl_sas_spec_support_show - sas spec supported
 * @cdev: pointer to embedded class device
 * @buf: the buffer returned
 *
 * A sysfs 'read-only' shost attribute.
 */
static ssize_t pm8001_ctl_sas_spec_support_show(struct device *cdev,
	struct device_attribute *attr, char *buf)
{
	unsigned int mode;
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct sas_ha_struct *sha = SHOST_TO_SAS_HA(shost);
	struct pm8001_hba_info *pm8001_ha = sha->lldd_ha;
	/* fe000000 means supports SAS2.1 */
	if (pm8001_ha->chip_id == chip_8001)
		mode = (pm8001_ha->main_cfg_tbl.pm8001_tbl.ctrl_cap_flag &
							0xfe000000)>>25;
	else
		/* fe000000 means supports SAS2.1 */
		mode = (pm8001_ha->main_cfg_tbl.pm80xx_tbl.ctrl_cap_flag &
							0xfe000000)>>25;
	return show_sas_spec_support_status(mode, buf);
}
static DEVICE_ATTR(sas_spec_support, S_IRUGO,
		   pm8001_ctl_sas_spec_support_show, NULL);

/**
 * pm8001_ctl_sas_address_show - sas address
 * @cdev: pointer to embedded class device
 * @buf: the buffer returned
 *
 * This is the controller sas address
 *
 * A sysfs 'read-only' shost attribute.
 */
static ssize_t pm8001_ctl_host_sas_address_show(struct device *cdev,
	struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct sas_ha_struct *sha = SHOST_TO_SAS_HA(shost);
	struct pm8001_hba_info *pm8001_ha = sha->lldd_ha;
	return snprintf(buf, PAGE_SIZE, "0x%016llx\n",
			be64_to_cpu(*(__be64 *)pm8001_ha->sas_addr));
}
static DEVICE_ATTR(host_sas_address, S_IRUGO,
		   pm8001_ctl_host_sas_address_show, NULL);

/**
 * pm8001_ctl_logging_level_show - logging level
 * @cdev: pointer to embedded class device
 * @buf: the buffer returned
 *
 * A sysfs 'read/write' shost attribute.
 */
static ssize_t pm8001_ctl_logging_level_show(struct device *cdev,
	struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct sas_ha_struct *sha = SHOST_TO_SAS_HA(shost);
	struct pm8001_hba_info *pm8001_ha = sha->lldd_ha;

	return snprintf(buf, PAGE_SIZE, "%08xh\n", pm8001_ha->logging_level);
}
static ssize_t pm8001_ctl_logging_level_store(struct device *cdev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct sas_ha_struct *sha = SHOST_TO_SAS_HA(shost);
	struct pm8001_hba_info *pm8001_ha = sha->lldd_ha;
	int val = 0;

	if (sscanf(buf, "%x", &val) != 1)
		return -EINVAL;

	pm8001_ha->logging_level = val;
	return strlen(buf);
}

static DEVICE_ATTR(logging_level, S_IRUGO | S_IWUSR,
	pm8001_ctl_logging_level_show, pm8001_ctl_logging_level_store);
/**
 * pm8001_ctl_aap_log_show - aap1 event log
 * @cdev: pointer to embedded class device
 * @buf: the buffer returned
 *
 * A sysfs 'read-only' shost attribute.
 */
static ssize_t pm8001_ctl_aap_log_show(struct device *cdev,
	struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct sas_ha_struct *sha = SHOST_TO_SAS_HA(shost);
	struct pm8001_hba_info *pm8001_ha = sha->lldd_ha;
	int i;
#define AAP1_MEMMAP(r, c) \
	(*(u32 *)((u8*)pm8001_ha->memoryMap.region[AAP1].virt_ptr + (r) * 32 \
	+ (c)))

	char *str = buf;
	int max = 2;
	for (i = 0; i < max; i++) {
		str += sprintf(str, "0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x"
			       "0x%08x 0x%08x\n",
			       AAP1_MEMMAP(i, 0),
			       AAP1_MEMMAP(i, 4),
			       AAP1_MEMMAP(i, 8),
			       AAP1_MEMMAP(i, 12),
			       AAP1_MEMMAP(i, 16),
			       AAP1_MEMMAP(i, 20),
			       AAP1_MEMMAP(i, 24),
			       AAP1_MEMMAP(i, 28));
	}

	return str - buf;
}
static DEVICE_ATTR(aap_log, S_IRUGO, pm8001_ctl_aap_log_show, NULL);
/**
 * pm8001_ctl_ib_queue_log_show - Out bound Queue log
 * @cdev:pointer to embedded class device
 * @buf: the buffer returned
 * A sysfs 'read-only' shost attribute.
 */
static ssize_t pm8001_ctl_ib_queue_log_show(struct device *cdev,
	struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct sas_ha_struct *sha = SHOST_TO_SAS_HA(shost);
	struct pm8001_hba_info *pm8001_ha = sha->lldd_ha;
	int offset;
	char *str = buf;
	int start = 0;
#define IB_MEMMAP(c)	\
		(*(u32 *)((u8 *)pm8001_ha->	\
		memoryMap.region[IB].virt_ptr +	\
		pm8001_ha->evtlog_ib_offset + (c)))

	for (offset = 0; offset < IB_OB_READ_TIMES; offset++) {
		str += sprintf(str, "0x%08x\n", IB_MEMMAP(start));
		start = start + 4;
	}
	pm8001_ha->evtlog_ib_offset += SYSFS_OFFSET;
	if (((pm8001_ha->evtlog_ib_offset) % (PM80XX_IB_OB_QUEUE_SIZE)) == 0)
		pm8001_ha->evtlog_ib_offset = 0;

	return str - buf;
}

static DEVICE_ATTR(ib_log, S_IRUGO, pm8001_ctl_ib_queue_log_show, NULL);
/**
 * pm8001_ctl_ob_queue_log_show - Out bound Queue log
 * @cdev:pointer to embedded class device
 * @buf: the buffer returned
 * A sysfs 'read-only' shost attribute.
 */

static ssize_t pm8001_ctl_ob_queue_log_show(struct device *cdev,
	struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct sas_ha_struct *sha = SHOST_TO_SAS_HA(shost);
	struct pm8001_hba_info *pm8001_ha = sha->lldd_ha;
	int offset;
	char *str = buf;
	int start = 0;
#define OB_MEMMAP(c)	\
		(*(u32 *)((u8 *)pm8001_ha->	\
		memoryMap.region[OB].virt_ptr +	\
		pm8001_ha->evtlog_ob_offset + (c)))

	for (offset = 0; offset < IB_OB_READ_TIMES; offset++) {
		str += sprintf(str, "0x%08x\n", OB_MEMMAP(start));
		start = start + 4;
	}
	pm8001_ha->evtlog_ob_offset += SYSFS_OFFSET;
	if (((pm8001_ha->evtlog_ob_offset) % (PM80XX_IB_OB_QUEUE_SIZE)) == 0)
		pm8001_ha->evtlog_ob_offset = 0;

	return str - buf;
}
static DEVICE_ATTR(ob_log, S_IRUGO, pm8001_ctl_ob_queue_log_show, NULL);
/**
 * pm8001_ctl_bios_version_show - Bios version Display
 * @cdev:pointer to embedded class device
 * @buf:the buffer returned
 * A sysfs 'read-only' shost attribute.
 */
static ssize_t pm8001_ctl_bios_version_show(struct device *cdev,
	struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct sas_ha_struct *sha = SHOST_TO_SAS_HA(shost);
	struct pm8001_hba_info *pm8001_ha = sha->lldd_ha;
	char *str = buf;
	void *virt_addr;
	int bios_index;
	DECLARE_COMPLETION_ONSTACK(completion);
	struct pm8001_ioctl_payload payload;

	pm8001_ha->nvmd_completion = &completion;
	payload.minor_function = 7;
	payload.offset = 0;
	payload.length = 4096;
	payload.func_specific = kzalloc(4096, GFP_KERNEL);
	PM8001_CHIP_DISP->get_nvmd_req(pm8001_ha, &payload);
	wait_for_completion(&completion);
	virt_addr = pm8001_ha->memoryMap.region[NVMD].virt_ptr;
	for (bios_index = BIOSOFFSET; bios_index < BIOS_OFFSET_LIMIT;
		bios_index++)
		str += sprintf(str, "%c",
			*((u8 *)((u8 *)virt_addr+bios_index)));
	return str - buf;
}
static DEVICE_ATTR(bios_version, S_IRUGO, pm8001_ctl_bios_version_show, NULL);
/**
 * pm8001_ctl_aap_log_show - IOP event log
 * @cdev: pointer to embedded class device
 * @buf: the buffer returned
 *
 * A sysfs 'read-only' shost attribute.
 */
static ssize_t pm8001_ctl_iop_log_show(struct device *cdev,
	struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct sas_ha_struct *sha = SHOST_TO_SAS_HA(shost);
	struct pm8001_hba_info *pm8001_ha = sha->lldd_ha;
#define IOP_MEMMAP(r, c) \
	(*(u32 *)((u8*)pm8001_ha->memoryMap.region[IOP].virt_ptr + (r) * 32 \
	+ (c)))
	int i;
	char *str = buf;
	int max = 2;
	for (i = 0; i < max; i++) {
		str += sprintf(str, "0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x"
			       "0x%08x 0x%08x\n",
			       IOP_MEMMAP(i, 0),
			       IOP_MEMMAP(i, 4),
			       IOP_MEMMAP(i, 8),
			       IOP_MEMMAP(i, 12),
			       IOP_MEMMAP(i, 16),
			       IOP_MEMMAP(i, 20),
			       IOP_MEMMAP(i, 24),
			       IOP_MEMMAP(i, 28));
	}

	return str - buf;
}
static DEVICE_ATTR(iop_log, S_IRUGO, pm8001_ctl_iop_log_show, NULL);

/**
 ** pm8001_ctl_fatal_log_show - fatal error logging
 ** @cdev:pointer to embedded class device
 ** @buf: the buffer returned
 **
 ** A sysfs 'read-only' shost attribute.
 **/

static ssize_t pm8001_ctl_fatal_log_show(struct device *cdev,
	struct device_attribute *attr, char *buf)
{
	ssize_t count;

	count = pm80xx_get_fatal_dump(cdev, attr, buf);
	return count;
}

static DEVICE_ATTR(fatal_log, S_IRUGO, pm8001_ctl_fatal_log_show, NULL);


/**
 ** pm8001_ctl_gsm_log_show - gsm dump collection
 ** @cdev:pointer to embedded class device
 ** @buf: the buffer returned
 **A sysfs 'read-only' shost attribute.
 **/
static ssize_t pm8001_ctl_gsm_log_show(struct device *cdev,
	struct device_attribute *attr, char *buf)
{
	ssize_t count;

	count = pm8001_get_gsm_dump(cdev, SYSFS_OFFSET, buf);
	return count;
}

static DEVICE_ATTR(gsm_log, S_IRUGO, pm8001_ctl_gsm_log_show, NULL);

#define FLASH_CMD_NONE      0x00
#define FLASH_CMD_UPDATE    0x01
#define FLASH_CMD_SET_NVMD    0x02

struct flash_command {
     u8      command[8];
     int     code;
};

static struct flash_command flash_command_table[] =
{
     {"set_nvmd",    FLASH_CMD_SET_NVMD},
     {"update",      FLASH_CMD_UPDATE},
     {"",            FLASH_CMD_NONE} /* Last entry should be NULL. */
};

struct error_fw {
     char    *reason;
     int     err_code;
};

static struct error_fw flash_error_table[] =
{
     {"Failed to open fw image file",	FAIL_OPEN_BIOS_FILE},
     {"image header mismatch",		FLASH_UPDATE_HDR_ERR},
     {"image offset mismatch",		FLASH_UPDATE_OFFSET_ERR},
     {"image CRC Error",		FLASH_UPDATE_CRC_ERR},
     {"image length Error.",		FLASH_UPDATE_LENGTH_ERR},
     {"Failed to program flash chip",	FLASH_UPDATE_HW_ERR},
     {"Flash chip not supported.",	FLASH_UPDATE_DNLD_NOT_SUPPORTED},
     {"Flash update disabled.",		FLASH_UPDATE_DISABLED},
     {"Flash in progress",		FLASH_IN_PROGRESS},
     {"Image file size Error",		FAIL_FILE_SIZE},
     {"Input parameter error",		FAIL_PARAMETERS},
     {"Out of memory",			FAIL_OUT_MEMORY},
     {"OK", 0}	/* Last entry err_code = 0. */
};

static int pm8001_set_nvmd(struct pm8001_hba_info *pm8001_ha)
{
	struct pm8001_ioctl_payload	*payload;
	DECLARE_COMPLETION_ONSTACK(completion);
	u8		*ioctlbuffer = NULL;
	u32		length = 0;
	u32		ret = 0;

	length = 1024 * 5 + sizeof(*payload) - 1;
	ioctlbuffer = kzalloc(length, GFP_KERNEL);
	if (!ioctlbuffer)
		return -ENOMEM;
	if ((pm8001_ha->fw_image->size <= 0) ||
	    (pm8001_ha->fw_image->size > 4096)) {
		ret = FAIL_FILE_SIZE;
		goto out;
	}
	payload = (struct pm8001_ioctl_payload *)ioctlbuffer;
	memcpy((u8 *)&payload->func_specific, (u8 *)pm8001_ha->fw_image->data,
				pm8001_ha->fw_image->size);
	payload->length = pm8001_ha->fw_image->size;
	payload->id = 0;
	payload->minor_function = 0x1;
	pm8001_ha->nvmd_completion = &completion;
	ret = PM8001_CHIP_DISP->set_nvmd_req(pm8001_ha, payload);
	wait_for_completion(&completion);
out:
	kfree(ioctlbuffer);
	return ret;
}

static int pm8001_update_flash(struct pm8001_hba_info *pm8001_ha)
{
	struct pm8001_ioctl_payload	*payload;
	DECLARE_COMPLETION_ONSTACK(completion);
	u8		*ioctlbuffer = NULL;
	u32		length = 0;
	struct fw_control_info	*fwControl;
	u32		loopNumber, loopcount = 0;
	u32		sizeRead = 0;
	u32		partitionSize, partitionSizeTmp;
	u32		ret = 0;
	u32		partitionNumber = 0;
	struct pm8001_fw_image_header *image_hdr;

	length = 1024 * 16 + sizeof(*payload) - 1;
	ioctlbuffer = kzalloc(length, GFP_KERNEL);
	image_hdr = (struct pm8001_fw_image_header *)pm8001_ha->fw_image->data;
	if (!ioctlbuffer)
		return -ENOMEM;
	if (pm8001_ha->fw_image->size < 28) {
		ret = FAIL_FILE_SIZE;
		goto out;
	}

	while (sizeRead < pm8001_ha->fw_image->size) {
		partitionSizeTmp =
			*(u32 *)((u8 *)&image_hdr->image_length + sizeRead);
		partitionSize = be32_to_cpu(partitionSizeTmp);
		loopcount = (partitionSize + HEADER_LEN)/IOCTL_BUF_SIZE;
		if (loopcount % IOCTL_BUF_SIZE)
			loopcount++;
		if (loopcount == 0)
			loopcount++;
		for (loopNumber = 0; loopNumber < loopcount; loopNumber++) {
			payload = (struct pm8001_ioctl_payload *)ioctlbuffer;
			payload->length = 1024*16;
			payload->id = 0;
			fwControl =
			      (struct fw_control_info *)&payload->func_specific;
			fwControl->len = IOCTL_BUF_SIZE;   /* IN */
			fwControl->size = partitionSize + HEADER_LEN;/* IN */
			fwControl->retcode = 0;/* OUT */
			fwControl->offset = loopNumber * IOCTL_BUF_SIZE;/*OUT */

		/* for the last chunk of data in case file size is not even with
		4k, load only the rest*/
		if (((loopcount-loopNumber) == 1) &&
			((partitionSize + HEADER_LEN) % IOCTL_BUF_SIZE)) {
			fwControl->len =
				(partitionSize + HEADER_LEN) % IOCTL_BUF_SIZE;
			memcpy((u8 *)fwControl->buffer,
				(u8 *)pm8001_ha->fw_image->data + sizeRead,
				(partitionSize + HEADER_LEN) % IOCTL_BUF_SIZE);
			sizeRead +=
				(partitionSize + HEADER_LEN) % IOCTL_BUF_SIZE;
		} else {
			memcpy((u8 *)fwControl->buffer,
				(u8 *)pm8001_ha->fw_image->data + sizeRead,
				IOCTL_BUF_SIZE);
			sizeRead += IOCTL_BUF_SIZE;
		}

		pm8001_ha->nvmd_completion = &completion;
		ret = PM8001_CHIP_DISP->fw_flash_update_req(pm8001_ha, payload);
		wait_for_completion(&completion);
		if (ret || (fwControl->retcode > FLASH_UPDATE_IN_PROGRESS)) {
			ret = fwControl->retcode;
			kfree(ioctlbuffer);
			ioctlbuffer = NULL;
			break;
		}
	}
	if (ret)
		break;
	partitionNumber++;
}
out:
	kfree(ioctlbuffer);
	return ret;
}
static ssize_t pm8001_store_update_fw(struct device *cdev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct sas_ha_struct *sha = SHOST_TO_SAS_HA(shost);
	struct pm8001_hba_info *pm8001_ha = sha->lldd_ha;
	char *cmd_ptr, *filename_ptr;
	int res, i;
	int flash_command = FLASH_CMD_NONE;
	int err = 0;
	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	cmd_ptr = kzalloc(count*2, GFP_KERNEL);

	if (!cmd_ptr) {
		err = FAIL_OUT_MEMORY;
		goto out;
	}

	filename_ptr = cmd_ptr + count;
	res = sscanf(buf, "%s %s", cmd_ptr, filename_ptr);
	if (res != 2) {
		err = FAIL_PARAMETERS;
		goto out1;
	}

	for (i = 0; flash_command_table[i].code != FLASH_CMD_NONE; i++) {
		if (!memcmp(flash_command_table[i].command,
				 cmd_ptr, strlen(cmd_ptr))) {
			flash_command = flash_command_table[i].code;
			break;
		}
	}
	if (flash_command == FLASH_CMD_NONE) {
		err = FAIL_PARAMETERS;
		goto out1;
	}

	if (pm8001_ha->fw_status == FLASH_IN_PROGRESS) {
		err = FLASH_IN_PROGRESS;
		goto out1;
	}
	err = request_firmware(&pm8001_ha->fw_image,
			       filename_ptr,
			       pm8001_ha->dev);

	if (err) {
		PM8001_FAIL_DBG(pm8001_ha,
			pm8001_printk("Failed to load firmware image file %s,"
			" error %d\n", filename_ptr, err));
		err = FAIL_OPEN_BIOS_FILE;
		goto out1;
	}

	switch (flash_command) {
	case FLASH_CMD_UPDATE:
		pm8001_ha->fw_status = FLASH_IN_PROGRESS;
		err = pm8001_update_flash(pm8001_ha);
		break;
	case FLASH_CMD_SET_NVMD:
		pm8001_ha->fw_status = FLASH_IN_PROGRESS;
		err = pm8001_set_nvmd(pm8001_ha);
		break;
	default:
		pm8001_ha->fw_status = FAIL_PARAMETERS;
		err = FAIL_PARAMETERS;
		break;
	}
	release_firmware(pm8001_ha->fw_image);
out1:
	kfree(cmd_ptr);
out:
	pm8001_ha->fw_status = err;

	if (!err)
		return count;
	else
		return -err;
}

static ssize_t pm8001_show_update_fw(struct device *cdev,
				     struct device_attribute *attr, char *buf)
{
	int i;
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct sas_ha_struct *sha = SHOST_TO_SAS_HA(shost);
	struct pm8001_hba_info *pm8001_ha = sha->lldd_ha;

	for (i = 0; flash_error_table[i].err_code != 0; i++) {
		if (flash_error_table[i].err_code == pm8001_ha->fw_status)
			break;
	}
	if (pm8001_ha->fw_status != FLASH_IN_PROGRESS)
		pm8001_ha->fw_status = FLASH_OK;

	return snprintf(buf, PAGE_SIZE, "status=%x %s\n",
			flash_error_table[i].err_code,
			flash_error_table[i].reason);
}

static DEVICE_ATTR(update_fw, S_IRUGO|S_IWUGO,
	pm8001_show_update_fw, pm8001_store_update_fw);
struct device_attribute *pm8001_host_attrs[] = {
	&dev_attr_interface_rev,
	&dev_attr_fw_version,
	&dev_attr_update_fw,
	&dev_attr_aap_log,
	&dev_attr_iop_log,
	&dev_attr_fatal_log,
	&dev_attr_gsm_log,
	&dev_attr_max_out_io,
	&dev_attr_max_devices,
	&dev_attr_max_sg_list,
	&dev_attr_sas_spec_support,
	&dev_attr_logging_level,
	&dev_attr_host_sas_address,
	&dev_attr_bios_version,
	&dev_attr_ib_log,
	&dev_attr_ob_log,
	NULL,
};

