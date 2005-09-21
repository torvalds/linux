/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Copyright (C) 2004-2005 Emulex.  All rights reserved.           *
 * EMULEX and SLI are trademarks of Emulex.                        *
 * www.emulex.com                                                  *
 * Portions Copyright (C) 2004-2005 Christoph Hellwig              *
 *                                                                 *
 * This program is free software; you can redistribute it and/or   *
 * modify it under the terms of version 2 of the GNU General       *
 * Public License as published by the Free Software Foundation.    *
 * This program is distributed in the hope that it will be useful. *
 * ALL EXPRESS OR IMPLIED CONDITIONS, REPRESENTATIONS AND          *
 * WARRANTIES, INCLUDING ANY IMPLIED WARRANTY OF MERCHANTABILITY,  *
 * FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT, ARE      *
 * DISCLAIMED, EXCEPT TO THE EXTENT THAT SUCH DISCLAIMERS ARE HELD *
 * TO BE LEGALLY INVALID.  See the GNU General Public License for  *
 * more details, a copy of which can be found in the file COPYING  *
 * included with this package.                                     *
 *******************************************************************/

#include <linux/ctype.h>
#include <linux/pci.h>
#include <linux/interrupt.h>

#include <scsi/scsi.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsi_transport_fc.h>

#include "lpfc_hw.h"
#include "lpfc_sli.h"
#include "lpfc_disc.h"
#include "lpfc_scsi.h"
#include "lpfc.h"
#include "lpfc_logmsg.h"
#include "lpfc_version.h"
#include "lpfc_compat.h"
#include "lpfc_crtn.h"


static void
lpfc_jedec_to_ascii(int incr, char hdw[])
{
	int i, j;
	for (i = 0; i < 8; i++) {
		j = (incr & 0xf);
		if (j <= 9)
			hdw[7 - i] = 0x30 +  j;
		 else
			hdw[7 - i] = 0x61 + j - 10;
		incr = (incr >> 4);
	}
	hdw[8] = 0;
	return;
}

static ssize_t
lpfc_drvr_version_show(struct class_device *cdev, char *buf)
{
	return snprintf(buf, PAGE_SIZE, LPFC_MODULE_DESC "\n");
}

static ssize_t
management_version_show(struct class_device *cdev, char *buf)
{
	return snprintf(buf, PAGE_SIZE, DFC_API_VERSION "\n");
}

static ssize_t
lpfc_info_show(struct class_device *cdev, char *buf)
{
	struct Scsi_Host *host = class_to_shost(cdev);
	return snprintf(buf, PAGE_SIZE, "%s\n",lpfc_info(host));
}

static ssize_t
lpfc_serialnum_show(struct class_device *cdev, char *buf)
{
	struct Scsi_Host *host = class_to_shost(cdev);
	struct lpfc_hba *phba = (struct lpfc_hba*)host->hostdata[0];
	return snprintf(buf, PAGE_SIZE, "%s\n",phba->SerialNumber);
}

static ssize_t
lpfc_modeldesc_show(struct class_device *cdev, char *buf)
{
	struct Scsi_Host *host = class_to_shost(cdev);
	struct lpfc_hba *phba = (struct lpfc_hba*)host->hostdata[0];
	return snprintf(buf, PAGE_SIZE, "%s\n",phba->ModelDesc);
}

static ssize_t
lpfc_modelname_show(struct class_device *cdev, char *buf)
{
	struct Scsi_Host *host = class_to_shost(cdev);
	struct lpfc_hba *phba = (struct lpfc_hba*)host->hostdata[0];
	return snprintf(buf, PAGE_SIZE, "%s\n",phba->ModelName);
}

static ssize_t
lpfc_programtype_show(struct class_device *cdev, char *buf)
{
	struct Scsi_Host *host = class_to_shost(cdev);
	struct lpfc_hba *phba = (struct lpfc_hba*)host->hostdata[0];
	return snprintf(buf, PAGE_SIZE, "%s\n",phba->ProgramType);
}

static ssize_t
lpfc_portnum_show(struct class_device *cdev, char *buf)
{
	struct Scsi_Host *host = class_to_shost(cdev);
	struct lpfc_hba *phba = (struct lpfc_hba*)host->hostdata[0];
	return snprintf(buf, PAGE_SIZE, "%s\n",phba->Port);
}

static ssize_t
lpfc_fwrev_show(struct class_device *cdev, char *buf)
{
	struct Scsi_Host *host = class_to_shost(cdev);
	struct lpfc_hba *phba = (struct lpfc_hba*)host->hostdata[0];
	char fwrev[32];
	lpfc_decode_firmware_rev(phba, fwrev, 1);
	return snprintf(buf, PAGE_SIZE, "%s\n",fwrev);
}

static ssize_t
lpfc_hdw_show(struct class_device *cdev, char *buf)
{
	char hdw[9];
	struct Scsi_Host *host = class_to_shost(cdev);
	struct lpfc_hba *phba = (struct lpfc_hba*)host->hostdata[0];
	lpfc_vpd_t *vp = &phba->vpd;
	lpfc_jedec_to_ascii(vp->rev.biuRev, hdw);
	return snprintf(buf, PAGE_SIZE, "%s\n", hdw);
}
static ssize_t
lpfc_option_rom_version_show(struct class_device *cdev, char *buf)
{
	struct Scsi_Host *host = class_to_shost(cdev);
	struct lpfc_hba *phba = (struct lpfc_hba*)host->hostdata[0];
	return snprintf(buf, PAGE_SIZE, "%s\n", phba->OptionROMVersion);
}
static ssize_t
lpfc_state_show(struct class_device *cdev, char *buf)
{
	struct Scsi_Host *host = class_to_shost(cdev);
	struct lpfc_hba *phba = (struct lpfc_hba*)host->hostdata[0];
	int len = 0;
	switch (phba->hba_state) {
	case LPFC_INIT_START:
	case LPFC_INIT_MBX_CMDS:
	case LPFC_LINK_DOWN:
		len += snprintf(buf + len, PAGE_SIZE-len, "Link Down\n");
		break;
	case LPFC_LINK_UP:
	case LPFC_LOCAL_CFG_LINK:
		len += snprintf(buf + len, PAGE_SIZE-len, "Link Up\n");
		break;
	case LPFC_FLOGI:
	case LPFC_FABRIC_CFG_LINK:
	case LPFC_NS_REG:
	case LPFC_NS_QRY:
	case LPFC_BUILD_DISC_LIST:
	case LPFC_DISC_AUTH:
	case LPFC_CLEAR_LA:
		len += snprintf(buf + len, PAGE_SIZE-len,
				"Link Up - Discovery\n");
		break;
	case LPFC_HBA_READY:
		len += snprintf(buf + len, PAGE_SIZE-len,
				"Link Up - Ready:\n");
		if (phba->fc_topology == TOPOLOGY_LOOP) {
			if (phba->fc_flag & FC_PUBLIC_LOOP)
				len += snprintf(buf + len, PAGE_SIZE-len,
						"   Public Loop\n");
			else
				len += snprintf(buf + len, PAGE_SIZE-len,
						"   Private Loop\n");
		} else {
			if (phba->fc_flag & FC_FABRIC)
				len += snprintf(buf + len, PAGE_SIZE-len,
						"   Fabric\n");
			else
				len += snprintf(buf + len, PAGE_SIZE-len,
						"   Point-2-Point\n");
		}
	}
	return len;
}

static ssize_t
lpfc_num_discovered_ports_show(struct class_device *cdev, char *buf)
{
	struct Scsi_Host *host = class_to_shost(cdev);
	struct lpfc_hba *phba = (struct lpfc_hba*)host->hostdata[0];
	return snprintf(buf, PAGE_SIZE, "%d\n", phba->fc_map_cnt +
							phba->fc_unmap_cnt);
}


static ssize_t
lpfc_issue_lip (struct class_device *cdev, const char *buf, size_t count)
{
	struct Scsi_Host *host = class_to_shost(cdev);
	struct lpfc_hba *phba = (struct lpfc_hba *) host->hostdata[0];
	int val = 0;
	LPFC_MBOXQ_t *pmboxq;
	int mbxstatus = MBXERR_ERROR;

	if ((sscanf(buf, "%d", &val) != 1) ||
	    (val != 1))
		return -EINVAL;

	if ((phba->fc_flag & FC_OFFLINE_MODE) ||
	    (phba->hba_state != LPFC_HBA_READY))
		return -EPERM;

	pmboxq = mempool_alloc(phba->mbox_mem_pool,GFP_KERNEL);

	if (!pmboxq)
		return -ENOMEM;

	memset((void *)pmboxq, 0, sizeof (LPFC_MBOXQ_t));
	lpfc_init_link(phba, pmboxq, phba->cfg_topology, phba->cfg_link_speed);
	mbxstatus = lpfc_sli_issue_mbox_wait(phba, pmboxq, phba->fc_ratov * 2);

	if (mbxstatus == MBX_TIMEOUT)
		pmboxq->mbox_cmpl = lpfc_sli_def_mbox_cmpl;
	else
		mempool_free( pmboxq, phba->mbox_mem_pool);

	if (mbxstatus == MBXERR_ERROR)
		return -EIO;

	return strlen(buf);
}

static ssize_t
lpfc_nport_evt_cnt_show(struct class_device *cdev, char *buf)
{
	struct Scsi_Host *host = class_to_shost(cdev);
	struct lpfc_hba *phba = (struct lpfc_hba*)host->hostdata[0];
	return snprintf(buf, PAGE_SIZE, "%d\n", phba->nport_event_cnt);
}

static ssize_t
lpfc_board_online_show(struct class_device *cdev, char *buf)
{
	struct Scsi_Host *host = class_to_shost(cdev);
	struct lpfc_hba *phba = (struct lpfc_hba*)host->hostdata[0];

	if (!phba) return 0;

	if (phba->fc_flag & FC_OFFLINE_MODE)
		return snprintf(buf, PAGE_SIZE, "0\n");
	else
		return snprintf(buf, PAGE_SIZE, "1\n");
}

static ssize_t
lpfc_board_online_store(struct class_device *cdev, const char *buf,
								size_t count)
{
	struct Scsi_Host *host = class_to_shost(cdev);
	struct lpfc_hba *phba = (struct lpfc_hba*)host->hostdata[0];
	struct completion online_compl;
	int val=0, status=0;

	if (sscanf(buf, "%d", &val) != 1)
		return 0;

	init_completion(&online_compl);

	if (val)
		lpfc_workq_post_event(phba, &status, &online_compl,
							LPFC_EVT_ONLINE);
	else
		lpfc_workq_post_event(phba, &status, &online_compl,
							LPFC_EVT_OFFLINE);
	wait_for_completion(&online_compl);
	if (!status)
		return strlen(buf);
	else
		return 0;
}


#define lpfc_param_show(attr)	\
static ssize_t \
lpfc_##attr##_show(struct class_device *cdev, char *buf) \
{ \
	struct Scsi_Host *host = class_to_shost(cdev);\
	struct lpfc_hba *phba = (struct lpfc_hba*)host->hostdata[0];\
	int val = 0;\
	if (phba){\
		val = phba->cfg_##attr;\
		return snprintf(buf, PAGE_SIZE, "%d\n",\
				phba->cfg_##attr);\
	}\
	return 0;\
}

#define lpfc_param_store(attr, minval, maxval)	\
static ssize_t \
lpfc_##attr##_store(struct class_device *cdev, const char *buf, size_t count) \
{ \
	struct Scsi_Host *host = class_to_shost(cdev);\
	struct lpfc_hba *phba = (struct lpfc_hba*)host->hostdata[0];\
	int val = 0;\
	if (!isdigit(buf[0]))\
		return -EINVAL;\
	if (sscanf(buf, "0x%x", &val) != 1)\
		if (sscanf(buf, "%d", &val) != 1)\
			return -EINVAL;\
	if (phba){\
		if (val >= minval && val <= maxval) {\
			phba->cfg_##attr = val;\
			return strlen(buf);\
		}\
	}\
	return 0;\
}

#define LPFC_ATTR_R_NOINIT(name, desc) \
extern int lpfc_##name;\
module_param(lpfc_##name, int, 0);\
MODULE_PARM_DESC(lpfc_##name, desc);\
lpfc_param_show(name)\
static CLASS_DEVICE_ATTR(lpfc_##name, S_IRUGO , lpfc_##name##_show, NULL)

#define LPFC_ATTR_R(name, defval, minval, maxval, desc) \
static int lpfc_##name = defval;\
module_param(lpfc_##name, int, 0);\
MODULE_PARM_DESC(lpfc_##name, desc);\
lpfc_param_show(name)\
static CLASS_DEVICE_ATTR(lpfc_##name, S_IRUGO , lpfc_##name##_show, NULL)

#define LPFC_ATTR_RW(name, defval, minval, maxval, desc) \
static int lpfc_##name = defval;\
module_param(lpfc_##name, int, 0);\
MODULE_PARM_DESC(lpfc_##name, desc);\
lpfc_param_show(name)\
lpfc_param_store(name, minval, maxval)\
static CLASS_DEVICE_ATTR(lpfc_##name, S_IRUGO | S_IWUSR,\
			 lpfc_##name##_show, lpfc_##name##_store)

static CLASS_DEVICE_ATTR(info, S_IRUGO, lpfc_info_show, NULL);
static CLASS_DEVICE_ATTR(serialnum, S_IRUGO, lpfc_serialnum_show, NULL);
static CLASS_DEVICE_ATTR(modeldesc, S_IRUGO, lpfc_modeldesc_show, NULL);
static CLASS_DEVICE_ATTR(modelname, S_IRUGO, lpfc_modelname_show, NULL);
static CLASS_DEVICE_ATTR(programtype, S_IRUGO, lpfc_programtype_show, NULL);
static CLASS_DEVICE_ATTR(portnum, S_IRUGO, lpfc_portnum_show, NULL);
static CLASS_DEVICE_ATTR(fwrev, S_IRUGO, lpfc_fwrev_show, NULL);
static CLASS_DEVICE_ATTR(hdw, S_IRUGO, lpfc_hdw_show, NULL);
static CLASS_DEVICE_ATTR(state, S_IRUGO, lpfc_state_show, NULL);
static CLASS_DEVICE_ATTR(option_rom_version, S_IRUGO,
					lpfc_option_rom_version_show, NULL);
static CLASS_DEVICE_ATTR(num_discovered_ports, S_IRUGO,
					lpfc_num_discovered_ports_show, NULL);
static CLASS_DEVICE_ATTR(nport_evt_cnt, S_IRUGO, lpfc_nport_evt_cnt_show, NULL);
static CLASS_DEVICE_ATTR(lpfc_drvr_version, S_IRUGO, lpfc_drvr_version_show,
			 NULL);
static CLASS_DEVICE_ATTR(management_version, S_IRUGO, management_version_show,
			 NULL);
static CLASS_DEVICE_ATTR(issue_lip, S_IWUSR, NULL, lpfc_issue_lip);
static CLASS_DEVICE_ATTR(board_online, S_IRUGO | S_IWUSR,
			 lpfc_board_online_show, lpfc_board_online_store);


/*
# lpfc_log_verbose: Only turn this flag on if you are willing to risk being
# deluged with LOTS of information.
# You can set a bit mask to record specific types of verbose messages:
#
# LOG_ELS                       0x1        ELS events
# LOG_DISCOVERY                 0x2        Link discovery events
# LOG_MBOX                      0x4        Mailbox events
# LOG_INIT                      0x8        Initialization events
# LOG_LINK_EVENT                0x10       Link events
# LOG_IP                        0x20       IP traffic history
# LOG_FCP                       0x40       FCP traffic history
# LOG_NODE                      0x80       Node table events
# LOG_MISC                      0x400      Miscellaneous events
# LOG_SLI                       0x800      SLI events
# LOG_CHK_COND                  0x1000     FCP Check condition flag
# LOG_LIBDFC                    0x2000     LIBDFC events
# LOG_ALL_MSG                   0xffff     LOG all messages
*/
LPFC_ATTR_RW(log_verbose, 0x0, 0x0, 0xffff, "Verbose logging bit-mask");

/*
# lun_queue_depth:  This parameter is used to limit the number of outstanding
# commands per FCP LUN. Value range is [1,128]. Default value is 30.
*/
LPFC_ATTR_R(lun_queue_depth, 30, 1, 128,
	    "Max number of FCP commands we can queue to a specific LUN");

/*
# Some disk devices have a "select ID" or "select Target" capability.
# From a protocol standpoint "select ID" usually means select the
# Fibre channel "ALPA".  In the FC-AL Profile there is an "informative
# annex" which contains a table that maps a "select ID" (a number
# between 0 and 7F) to an ALPA.  By default, for compatibility with
# older drivers, the lpfc driver scans this table from low ALPA to high
# ALPA.
#
# Turning on the scan-down variable (on  = 1, off = 0) will
# cause the lpfc driver to use an inverted table, effectively
# scanning ALPAs from high to low. Value range is [0,1]. Default value is 1.
#
# (Note: This "select ID" functionality is a LOOP ONLY characteristic
# and will not work across a fabric. Also this parameter will take
# effect only in the case when ALPA map is not available.)
*/
LPFC_ATTR_R(scan_down, 1, 0, 1,
	     "Start scanning for devices from highest ALPA to lowest");

/*
# lpfc_nodev_tmo: If set, it will hold all I/O errors on devices that disappear
# until the timer expires. Value range is [0,255]. Default value is 20.
# NOTE: this MUST be less then the SCSI Layer command timeout - 1.
*/
LPFC_ATTR_RW(nodev_tmo, 30, 0, 255,
	     "Seconds driver will hold I/O waiting for a device to come back");

/*
# lpfc_topology:  link topology for init link
#            0x0  = attempt loop mode then point-to-point
#            0x02 = attempt point-to-point mode only
#            0x04 = attempt loop mode only
#            0x06 = attempt point-to-point mode then loop
# Set point-to-point mode if you want to run as an N_Port.
# Set loop mode if you want to run as an NL_Port. Value range is [0,0x6].
# Default value is 0.
*/
LPFC_ATTR_R(topology, 0, 0, 6, "Select Fibre Channel topology");

/*
# lpfc_link_speed: Link speed selection for initializing the Fibre Channel
# connection.
#       0  = auto select (default)
#       1  = 1 Gigabaud
#       2  = 2 Gigabaud
#       4  = 4 Gigabaud
# Value range is [0,4]. Default value is 0.
*/
LPFC_ATTR_R(link_speed, 0, 0, 4, "Select link speed");

/*
# lpfc_fcp_class:  Determines FC class to use for the FCP protocol.
# Value range is [2,3]. Default value is 3.
*/
LPFC_ATTR_R(fcp_class, 3, 2, 3,
	     "Select Fibre Channel class of service for FCP sequences");

/*
# lpfc_use_adisc: Use ADISC for FCP rediscovery instead of PLOGI. Value range
# is [0,1]. Default value is 0.
*/
LPFC_ATTR_RW(use_adisc, 0, 0, 1,
	     "Use ADISC on rediscovery to authenticate FCP devices");

/*
# lpfc_ack0: Use ACK0, instead of ACK1 for class 2 acknowledgement. Value
# range is [0,1]. Default value is 0.
*/
LPFC_ATTR_R(ack0, 0, 0, 1, "Enable ACK0 support");

/*
# lpfc_cr_delay & lpfc_cr_count: Default values for I/O colaesing
# cr_delay (msec) or cr_count outstanding commands. cr_delay can take
# value [0,63]. cr_count can take value [0,255]. Default value of cr_delay
# is 0. Default value of cr_count is 1. The cr_count feature is disabled if
# cr_delay is set to 0.
*/
static int lpfc_cr_delay = 0;
module_param(lpfc_cr_delay, int , 0);
MODULE_PARM_DESC(lpfc_cr_delay, "A count of milliseconds after which an "
		"interrupt response is generated");

static int lpfc_cr_count = 1;
module_param(lpfc_cr_count, int, 0);
MODULE_PARM_DESC(lpfc_cr_count, "A count of I/O completions after which an "
		"interrupt response is generated");

/*
# lpfc_fdmi_on: controls FDMI support.
#       0 = no FDMI support
#       1 = support FDMI without attribute of hostname
#       2 = support FDMI with attribute of hostname
# Value range [0,2]. Default value is 0.
*/
LPFC_ATTR_RW(fdmi_on, 0, 0, 2, "Enable FDMI support");

/*
# Specifies the maximum number of ELS cmds we can have outstanding (for
# discovery). Value range is [1,64]. Default value = 32.
*/
static int lpfc_discovery_threads = 32;
module_param(lpfc_discovery_threads, int, 0);
MODULE_PARM_DESC(lpfc_discovery_threads, "Maximum number of ELS commands "
		 "during discovery");

/*
# lpfc_max_luns: maximum number of LUNs per target driver will support
# Value range is [1,32768]. Default value is 256.
# NOTE: The SCSI layer will scan each target for this many luns
*/
LPFC_ATTR_R(max_luns, 256, 1, 32768,
	     "Maximum number of LUNs per target driver will support");

struct class_device_attribute *lpfc_host_attrs[] = {
	&class_device_attr_info,
	&class_device_attr_serialnum,
	&class_device_attr_modeldesc,
	&class_device_attr_modelname,
	&class_device_attr_programtype,
	&class_device_attr_portnum,
	&class_device_attr_fwrev,
	&class_device_attr_hdw,
	&class_device_attr_option_rom_version,
	&class_device_attr_state,
	&class_device_attr_num_discovered_ports,
	&class_device_attr_lpfc_drvr_version,
	&class_device_attr_lpfc_log_verbose,
	&class_device_attr_lpfc_lun_queue_depth,
	&class_device_attr_lpfc_nodev_tmo,
	&class_device_attr_lpfc_fcp_class,
	&class_device_attr_lpfc_use_adisc,
	&class_device_attr_lpfc_ack0,
	&class_device_attr_lpfc_topology,
	&class_device_attr_lpfc_scan_down,
	&class_device_attr_lpfc_link_speed,
	&class_device_attr_lpfc_fdmi_on,
	&class_device_attr_lpfc_max_luns,
	&class_device_attr_nport_evt_cnt,
	&class_device_attr_management_version,
	&class_device_attr_issue_lip,
	&class_device_attr_board_online,
	NULL,
};

static ssize_t
sysfs_ctlreg_write(struct kobject *kobj, char *buf, loff_t off, size_t count)
{
	size_t buf_off;
	struct Scsi_Host *host = class_to_shost(container_of(kobj,
					     struct class_device, kobj));
	struct lpfc_hba *phba = (struct lpfc_hba*)host->hostdata[0];

	if ((off + count) > FF_REG_AREA_SIZE)
		return -ERANGE;

	if (count == 0) return 0;

	if (off % 4 || count % 4 || (unsigned long)buf % 4)
		return -EINVAL;

	spin_lock_irq(phba->host->host_lock);

	if (!(phba->fc_flag & FC_OFFLINE_MODE)) {
		spin_unlock_irq(phba->host->host_lock);
		return -EPERM;
	}

	for (buf_off = 0; buf_off < count; buf_off += sizeof(uint32_t))
		writel(*((uint32_t *)(buf + buf_off)),
		       phba->ctrl_regs_memmap_p + off + buf_off);

	spin_unlock_irq(phba->host->host_lock);

	return count;
}

static ssize_t
sysfs_ctlreg_read(struct kobject *kobj, char *buf, loff_t off, size_t count)
{
	size_t buf_off;
	uint32_t * tmp_ptr;
	struct Scsi_Host *host = class_to_shost(container_of(kobj,
					     struct class_device, kobj));
	struct lpfc_hba *phba = (struct lpfc_hba*)host->hostdata[0];

	if (off > FF_REG_AREA_SIZE)
		return -ERANGE;

	if ((off + count) > FF_REG_AREA_SIZE)
		count = FF_REG_AREA_SIZE - off;

	if (count == 0) return 0;

	if (off % 4 || count % 4 || (unsigned long)buf % 4)
		return -EINVAL;

	spin_lock_irq(phba->host->host_lock);

	for (buf_off = 0; buf_off < count; buf_off += sizeof(uint32_t)) {
		tmp_ptr = (uint32_t *)(buf + buf_off);
		*tmp_ptr = readl(phba->ctrl_regs_memmap_p + off + buf_off);
	}

	spin_unlock_irq(phba->host->host_lock);

	return count;
}

static struct bin_attribute sysfs_ctlreg_attr = {
	.attr = {
		.name = "ctlreg",
		.mode = S_IRUSR | S_IWUSR,
		.owner = THIS_MODULE,
	},
	.size = 256,
	.read = sysfs_ctlreg_read,
	.write = sysfs_ctlreg_write,
};


static void
sysfs_mbox_idle (struct lpfc_hba * phba)
{
	phba->sysfs_mbox.state = SMBOX_IDLE;
	phba->sysfs_mbox.offset = 0;

	if (phba->sysfs_mbox.mbox) {
		mempool_free(phba->sysfs_mbox.mbox,
			     phba->mbox_mem_pool);
		phba->sysfs_mbox.mbox = NULL;
	}
}

static ssize_t
sysfs_mbox_write(struct kobject *kobj, char *buf, loff_t off, size_t count)
{
	struct Scsi_Host * host =
		class_to_shost(container_of(kobj, struct class_device, kobj));
	struct lpfc_hba * phba = (struct lpfc_hba*)host->hostdata[0];
	struct lpfcMboxq * mbox = NULL;

	if ((count + off) > MAILBOX_CMD_SIZE)
		return -ERANGE;

	if (off % 4 ||  count % 4 || (unsigned long)buf % 4)
		return -EINVAL;

	if (count == 0)
		return 0;

	if (off == 0) {
		mbox = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
		if (!mbox)
			return -ENOMEM;

	}

	spin_lock_irq(host->host_lock);

	if (off == 0) {
		if (phba->sysfs_mbox.mbox)
			mempool_free(mbox, phba->mbox_mem_pool);
		else
			phba->sysfs_mbox.mbox = mbox;
		phba->sysfs_mbox.state = SMBOX_WRITING;
	} else {
		if (phba->sysfs_mbox.state  != SMBOX_WRITING ||
		    phba->sysfs_mbox.offset != off           ||
		    phba->sysfs_mbox.mbox   == NULL ) {
			sysfs_mbox_idle(phba);
			spin_unlock_irq(host->host_lock);
			return -EINVAL;
		}
	}

	memcpy((uint8_t *) & phba->sysfs_mbox.mbox->mb + off,
	       buf, count);

	phba->sysfs_mbox.offset = off + count;

	spin_unlock_irq(host->host_lock);

	return count;
}

static ssize_t
sysfs_mbox_read(struct kobject *kobj, char *buf, loff_t off, size_t count)
{
	struct Scsi_Host *host =
		class_to_shost(container_of(kobj, struct class_device,
					    kobj));
	struct lpfc_hba *phba = (struct lpfc_hba*)host->hostdata[0];
	int rc;

	if (off > sizeof(MAILBOX_t))
		return -ERANGE;

	if ((count + off) > sizeof(MAILBOX_t))
		count = sizeof(MAILBOX_t) - off;

	if (off % 4 ||  count % 4 || (unsigned long)buf % 4)
		return -EINVAL;

	if (off && count == 0)
		return 0;

	spin_lock_irq(phba->host->host_lock);

	if (off == 0 &&
	    phba->sysfs_mbox.state  == SMBOX_WRITING &&
	    phba->sysfs_mbox.offset >= 2 * sizeof(uint32_t)) {

		switch (phba->sysfs_mbox.mbox->mb.mbxCommand) {
			/* Offline only */
		case MBX_WRITE_NV:
		case MBX_INIT_LINK:
		case MBX_DOWN_LINK:
		case MBX_CONFIG_LINK:
		case MBX_CONFIG_RING:
		case MBX_RESET_RING:
		case MBX_UNREG_LOGIN:
		case MBX_CLEAR_LA:
		case MBX_DUMP_CONTEXT:
		case MBX_RUN_DIAGS:
		case MBX_RESTART:
		case MBX_FLASH_WR_ULA:
		case MBX_SET_MASK:
		case MBX_SET_SLIM:
		case MBX_SET_DEBUG:
			if (!(phba->fc_flag & FC_OFFLINE_MODE)) {
				printk(KERN_WARNING "mbox_read:Command 0x%x "
				       "is illegal in on-line state\n",
				       phba->sysfs_mbox.mbox->mb.mbxCommand);
				sysfs_mbox_idle(phba);
				spin_unlock_irq(phba->host->host_lock);
				return -EPERM;
			}
		case MBX_LOAD_SM:
		case MBX_READ_NV:
		case MBX_READ_CONFIG:
		case MBX_READ_RCONFIG:
		case MBX_READ_STATUS:
		case MBX_READ_XRI:
		case MBX_READ_REV:
		case MBX_READ_LNK_STAT:
		case MBX_DUMP_MEMORY:
		case MBX_DOWN_LOAD:
		case MBX_UPDATE_CFG:
		case MBX_LOAD_AREA:
		case MBX_LOAD_EXP_ROM:
			break;
		case MBX_READ_SPARM64:
		case MBX_READ_LA:
		case MBX_READ_LA64:
		case MBX_REG_LOGIN:
		case MBX_REG_LOGIN64:
		case MBX_CONFIG_PORT:
		case MBX_RUN_BIU_DIAG:
			printk(KERN_WARNING "mbox_read: Illegal Command 0x%x\n",
			       phba->sysfs_mbox.mbox->mb.mbxCommand);
			sysfs_mbox_idle(phba);
			spin_unlock_irq(phba->host->host_lock);
			return -EPERM;
		default:
			printk(KERN_WARNING "mbox_read: Unknown Command 0x%x\n",
			       phba->sysfs_mbox.mbox->mb.mbxCommand);
			sysfs_mbox_idle(phba);
			spin_unlock_irq(phba->host->host_lock);
			return -EPERM;
		}

		if ((phba->fc_flag & FC_OFFLINE_MODE) ||
		    (!(phba->sli.sli_flag & LPFC_SLI2_ACTIVE))){

			spin_unlock_irq(phba->host->host_lock);
			rc = lpfc_sli_issue_mbox (phba,
						  phba->sysfs_mbox.mbox,
						  MBX_POLL);
			spin_lock_irq(phba->host->host_lock);

		} else {
			spin_unlock_irq(phba->host->host_lock);
			rc = lpfc_sli_issue_mbox_wait (phba,
						       phba->sysfs_mbox.mbox,
						       phba->fc_ratov * 2);
			spin_lock_irq(phba->host->host_lock);
		}

		if (rc != MBX_SUCCESS) {
			sysfs_mbox_idle(phba);
			spin_unlock_irq(host->host_lock);
			return -ENODEV;
		}
		phba->sysfs_mbox.state = SMBOX_READING;
	}
	else if (phba->sysfs_mbox.offset != off ||
		 phba->sysfs_mbox.state  != SMBOX_READING) {
		printk(KERN_WARNING  "mbox_read: Bad State\n");
		sysfs_mbox_idle(phba);
		spin_unlock_irq(host->host_lock);
		return -EINVAL;
	}

	memcpy(buf, (uint8_t *) & phba->sysfs_mbox.mbox->mb + off, count);

	phba->sysfs_mbox.offset = off + count;

	if (phba->sysfs_mbox.offset == sizeof(MAILBOX_t))
		sysfs_mbox_idle(phba);

	spin_unlock_irq(phba->host->host_lock);

	return count;
}

static struct bin_attribute sysfs_mbox_attr = {
	.attr = {
		.name = "mbox",
		.mode = S_IRUSR | S_IWUSR,
		.owner = THIS_MODULE,
	},
	.size = sizeof(MAILBOX_t),
	.read = sysfs_mbox_read,
	.write = sysfs_mbox_write,
};

int
lpfc_alloc_sysfs_attr(struct lpfc_hba *phba)
{
	struct Scsi_Host *host = phba->host;
	int error;

	error = sysfs_create_bin_file(&host->shost_classdev.kobj,
							&sysfs_ctlreg_attr);
	if (error)
		goto out;

	error = sysfs_create_bin_file(&host->shost_classdev.kobj,
							&sysfs_mbox_attr);
	if (error)
		goto out_remove_ctlreg_attr;

	return 0;
out_remove_ctlreg_attr:
	sysfs_remove_bin_file(&host->shost_classdev.kobj, &sysfs_ctlreg_attr);
out:
	return error;
}

void
lpfc_free_sysfs_attr(struct lpfc_hba *phba)
{
	struct Scsi_Host *host = phba->host;

	sysfs_remove_bin_file(&host->shost_classdev.kobj, &sysfs_mbox_attr);
	sysfs_remove_bin_file(&host->shost_classdev.kobj, &sysfs_ctlreg_attr);
}


/*
 * Dynamic FC Host Attributes Support
 */

static void
lpfc_get_host_port_id(struct Scsi_Host *shost)
{
	struct lpfc_hba *phba = (struct lpfc_hba*)shost->hostdata[0];
	/* note: fc_myDID already in cpu endianness */
	fc_host_port_id(shost) = phba->fc_myDID;
}

static void
lpfc_get_host_port_type(struct Scsi_Host *shost)
{
	struct lpfc_hba *phba = (struct lpfc_hba*)shost->hostdata[0];

	spin_lock_irq(shost->host_lock);

	if (phba->hba_state == LPFC_HBA_READY) {
		if (phba->fc_topology == TOPOLOGY_LOOP) {
			if (phba->fc_flag & FC_PUBLIC_LOOP)
				fc_host_port_type(shost) = FC_PORTTYPE_NLPORT;
			else
				fc_host_port_type(shost) = FC_PORTTYPE_LPORT;
		} else {
			if (phba->fc_flag & FC_FABRIC)
				fc_host_port_type(shost) = FC_PORTTYPE_NPORT;
			else
				fc_host_port_type(shost) = FC_PORTTYPE_PTP;
		}
	} else
		fc_host_port_type(shost) = FC_PORTTYPE_UNKNOWN;

	spin_unlock_irq(shost->host_lock);
}

static void
lpfc_get_host_port_state(struct Scsi_Host *shost)
{
	struct lpfc_hba *phba = (struct lpfc_hba*)shost->hostdata[0];

	spin_lock_irq(shost->host_lock);

	if (phba->fc_flag & FC_OFFLINE_MODE)
		fc_host_port_state(shost) = FC_PORTSTATE_OFFLINE;
	else {
		switch (phba->hba_state) {
		case LPFC_INIT_START:
		case LPFC_INIT_MBX_CMDS:
		case LPFC_LINK_DOWN:
			fc_host_port_state(shost) = FC_PORTSTATE_LINKDOWN;
			break;
		case LPFC_LINK_UP:
		case LPFC_LOCAL_CFG_LINK:
		case LPFC_FLOGI:
		case LPFC_FABRIC_CFG_LINK:
		case LPFC_NS_REG:
		case LPFC_NS_QRY:
		case LPFC_BUILD_DISC_LIST:
		case LPFC_DISC_AUTH:
		case LPFC_CLEAR_LA:
		case LPFC_HBA_READY:
			/* Links up, beyond this port_type reports state */
			fc_host_port_state(shost) = FC_PORTSTATE_ONLINE;
			break;
		case LPFC_HBA_ERROR:
			fc_host_port_state(shost) = FC_PORTSTATE_ERROR;
			break;
		default:
			fc_host_port_state(shost) = FC_PORTSTATE_UNKNOWN;
			break;
		}
	}

	spin_unlock_irq(shost->host_lock);
}

static void
lpfc_get_host_speed(struct Scsi_Host *shost)
{
	struct lpfc_hba *phba = (struct lpfc_hba*)shost->hostdata[0];

	spin_lock_irq(shost->host_lock);

	if (phba->hba_state == LPFC_HBA_READY) {
		switch(phba->fc_linkspeed) {
			case LA_1GHZ_LINK:
				fc_host_speed(shost) = FC_PORTSPEED_1GBIT;
			break;
			case LA_2GHZ_LINK:
				fc_host_speed(shost) = FC_PORTSPEED_2GBIT;
			break;
			case LA_4GHZ_LINK:
				fc_host_speed(shost) = FC_PORTSPEED_4GBIT;
			break;
			default:
				fc_host_speed(shost) = FC_PORTSPEED_UNKNOWN;
			break;
		}
	}

	spin_unlock_irq(shost->host_lock);
}

static void
lpfc_get_host_fabric_name (struct Scsi_Host *shost)
{
	struct lpfc_hba *phba = (struct lpfc_hba*)shost->hostdata[0];
	u64 node_name;

	spin_lock_irq(shost->host_lock);

	if ((phba->fc_flag & FC_FABRIC) ||
	    ((phba->fc_topology == TOPOLOGY_LOOP) &&
	     (phba->fc_flag & FC_PUBLIC_LOOP)))
		node_name = wwn_to_u64(phba->fc_fabparam.nodeName.u.wwn);
	else
		/* fabric is local port if there is no F/FL_Port */
		node_name = wwn_to_u64(phba->fc_nodename.u.wwn);

	spin_unlock_irq(shost->host_lock);

	fc_host_fabric_name(shost) = node_name;
}


static struct fc_host_statistics *
lpfc_get_stats(struct Scsi_Host *shost)
{
	struct lpfc_hba *phba = (struct lpfc_hba *)shost->hostdata[0];
	struct lpfc_sli *psli = &phba->sli;
	struct fc_host_statistics *hs = &phba->link_stats;
	LPFC_MBOXQ_t *pmboxq;
	MAILBOX_t *pmb;
	int rc=0;

	pmboxq = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
	if (!pmboxq)
		return NULL;
	memset(pmboxq, 0, sizeof (LPFC_MBOXQ_t));

	pmb = &pmboxq->mb;
	pmb->mbxCommand = MBX_READ_STATUS;
	pmb->mbxOwner = OWN_HOST;
	pmboxq->context1 = NULL;

	if ((phba->fc_flag & FC_OFFLINE_MODE) ||
	    (!(psli->sli_flag & LPFC_SLI2_ACTIVE))){
		rc = lpfc_sli_issue_mbox(phba, pmboxq, MBX_POLL);
	} else
		rc = lpfc_sli_issue_mbox_wait(phba, pmboxq, phba->fc_ratov * 2);

	if (rc != MBX_SUCCESS) {
		if (pmboxq) {
			if (rc == MBX_TIMEOUT)
				pmboxq->mbox_cmpl = lpfc_sli_def_mbox_cmpl;
			else
				mempool_free( pmboxq, phba->mbox_mem_pool);
		}
		return NULL;
	}

	memset(hs, 0, sizeof (struct fc_host_statistics));

	hs->tx_frames = pmb->un.varRdStatus.xmitFrameCnt;
	hs->tx_words = (pmb->un.varRdStatus.xmitByteCnt * 256);
	hs->rx_frames = pmb->un.varRdStatus.rcvFrameCnt;
	hs->rx_words = (pmb->un.varRdStatus.rcvByteCnt * 256);

	memset((void *)pmboxq, 0, sizeof (LPFC_MBOXQ_t));
	pmb->mbxCommand = MBX_READ_LNK_STAT;
	pmb->mbxOwner = OWN_HOST;
	pmboxq->context1 = NULL;

	if ((phba->fc_flag & FC_OFFLINE_MODE) ||
	    (!(psli->sli_flag & LPFC_SLI2_ACTIVE))) {
		rc = lpfc_sli_issue_mbox(phba, pmboxq, MBX_POLL);
	} else
		rc = lpfc_sli_issue_mbox_wait(phba, pmboxq, phba->fc_ratov * 2);

	if (rc != MBX_SUCCESS) {
		if (pmboxq) {
			if (rc == MBX_TIMEOUT)
				pmboxq->mbox_cmpl = lpfc_sli_def_mbox_cmpl;
			else
				mempool_free( pmboxq, phba->mbox_mem_pool);
		}
		return NULL;
	}

	hs->link_failure_count = pmb->un.varRdLnk.linkFailureCnt;
	hs->loss_of_sync_count = pmb->un.varRdLnk.lossSyncCnt;
	hs->loss_of_signal_count = pmb->un.varRdLnk.lossSignalCnt;
	hs->prim_seq_protocol_err_count = pmb->un.varRdLnk.primSeqErrCnt;
	hs->invalid_tx_word_count = pmb->un.varRdLnk.invalidXmitWord;
	hs->invalid_crc_count = pmb->un.varRdLnk.crcCnt;
	hs->error_frames = pmb->un.varRdLnk.crcCnt;

	if (phba->fc_topology == TOPOLOGY_LOOP) {
		hs->lip_count = (phba->fc_eventTag >> 1);
		hs->nos_count = -1;
	} else {
		hs->lip_count = -1;
		hs->nos_count = (phba->fc_eventTag >> 1);
	}

	hs->dumped_frames = -1;

/* FIX ME */
	/*hs->SecondsSinceLastReset = (jiffies - lpfc_loadtime) / HZ;*/

	return hs;
}


/*
 * The LPFC driver treats linkdown handling as target loss events so there
 * are no sysfs handlers for link_down_tmo.
 */
static void
lpfc_get_starget_port_id(struct scsi_target *starget)
{
	struct Scsi_Host *shost = dev_to_shost(starget->dev.parent);
	struct lpfc_hba *phba = (struct lpfc_hba *) shost->hostdata[0];
	uint32_t did = -1;
	struct lpfc_nodelist *ndlp = NULL;

	spin_lock_irq(shost->host_lock);
	/* Search the mapped list for this target ID */
	list_for_each_entry(ndlp, &phba->fc_nlpmap_list, nlp_listp) {
		if (starget->id == ndlp->nlp_sid) {
			did = ndlp->nlp_DID;
			break;
		}
	}
	spin_unlock_irq(shost->host_lock);

	fc_starget_port_id(starget) = did;
}

static void
lpfc_get_starget_node_name(struct scsi_target *starget)
{
	struct Scsi_Host *shost = dev_to_shost(starget->dev.parent);
	struct lpfc_hba *phba = (struct lpfc_hba *) shost->hostdata[0];
	u64 node_name = 0;
	struct lpfc_nodelist *ndlp = NULL;

	spin_lock_irq(shost->host_lock);
	/* Search the mapped list for this target ID */
	list_for_each_entry(ndlp, &phba->fc_nlpmap_list, nlp_listp) {
		if (starget->id == ndlp->nlp_sid) {
			node_name = wwn_to_u64(ndlp->nlp_nodename.u.wwn);
			break;
		}
	}
	spin_unlock_irq(shost->host_lock);

	fc_starget_node_name(starget) = node_name;
}

static void
lpfc_get_starget_port_name(struct scsi_target *starget)
{
	struct Scsi_Host *shost = dev_to_shost(starget->dev.parent);
	struct lpfc_hba *phba = (struct lpfc_hba *) shost->hostdata[0];
	u64 port_name = 0;
	struct lpfc_nodelist *ndlp = NULL;

	spin_lock_irq(shost->host_lock);
	/* Search the mapped list for this target ID */
	list_for_each_entry(ndlp, &phba->fc_nlpmap_list, nlp_listp) {
		if (starget->id == ndlp->nlp_sid) {
			port_name = wwn_to_u64(ndlp->nlp_portname.u.wwn);
			break;
		}
	}
	spin_unlock_irq(shost->host_lock);

	fc_starget_port_name(starget) = port_name;
}

static void
lpfc_get_rport_loss_tmo(struct fc_rport *rport)
{
	/*
	 * Return the driver's global value for device loss timeout plus
	 * five seconds to allow the driver's nodev timer to run.
	 */
	rport->dev_loss_tmo = lpfc_nodev_tmo + 5;
}

static void
lpfc_set_rport_loss_tmo(struct fc_rport *rport, uint32_t timeout)
{
	/*
	 * The driver doesn't have a per-target timeout setting.  Set
	 * this value globally. lpfc_nodev_tmo should be greater then 0.
	 */
	if (timeout)
		lpfc_nodev_tmo = timeout;
	else
		lpfc_nodev_tmo = 1;
	rport->dev_loss_tmo = lpfc_nodev_tmo + 5;
}


#define lpfc_rport_show_function(field, format_string, sz, cast)	\
static ssize_t								\
lpfc_show_rport_##field (struct class_device *cdev, char *buf)		\
{									\
	struct fc_rport *rport = transport_class_to_rport(cdev);	\
	struct lpfc_rport_data *rdata = rport->hostdata;		\
	return snprintf(buf, sz, format_string,				\
		(rdata->target) ? cast rdata->target->field : 0);	\
}

#define lpfc_rport_rd_attr(field, format_string, sz)			\
	lpfc_rport_show_function(field, format_string, sz, )		\
static FC_RPORT_ATTR(field, S_IRUGO, lpfc_show_rport_##field, NULL)


struct fc_function_template lpfc_transport_functions = {
	/* fixed attributes the driver supports */
	.show_host_node_name = 1,
	.show_host_port_name = 1,
	.show_host_supported_classes = 1,
	.show_host_supported_fc4s = 1,
	.show_host_symbolic_name = 1,
	.show_host_supported_speeds = 1,
	.show_host_maxframe_size = 1,

	/* dynamic attributes the driver supports */
	.get_host_port_id = lpfc_get_host_port_id,
	.show_host_port_id = 1,

	.get_host_port_type = lpfc_get_host_port_type,
	.show_host_port_type = 1,

	.get_host_port_state = lpfc_get_host_port_state,
	.show_host_port_state = 1,

	/* active_fc4s is shown but doesn't change (thus no get function) */
	.show_host_active_fc4s = 1,

	.get_host_speed = lpfc_get_host_speed,
	.show_host_speed = 1,

	.get_host_fabric_name = lpfc_get_host_fabric_name,
	.show_host_fabric_name = 1,

	/*
	 * The LPFC driver treats linkdown handling as target loss events
	 * so there are no sysfs handlers for link_down_tmo.
	 */

	.get_fc_host_stats = lpfc_get_stats,

	/* the LPFC driver doesn't support resetting stats yet */

	.dd_fcrport_size = sizeof(struct lpfc_rport_data),
	.show_rport_maxframe_size = 1,
	.show_rport_supported_classes = 1,

	.get_rport_dev_loss_tmo = lpfc_get_rport_loss_tmo,
	.set_rport_dev_loss_tmo = lpfc_set_rport_loss_tmo,
	.show_rport_dev_loss_tmo = 1,

	.get_starget_port_id  = lpfc_get_starget_port_id,
	.show_starget_port_id = 1,

	.get_starget_node_name = lpfc_get_starget_node_name,
	.show_starget_node_name = 1,

	.get_starget_port_name = lpfc_get_starget_port_name,
	.show_starget_port_name = 1,
};

void
lpfc_get_cfgparam(struct lpfc_hba *phba)
{
	phba->cfg_log_verbose = lpfc_log_verbose;
	phba->cfg_cr_delay = lpfc_cr_delay;
	phba->cfg_cr_count = lpfc_cr_count;
	phba->cfg_lun_queue_depth = lpfc_lun_queue_depth;
	phba->cfg_fcp_class = lpfc_fcp_class;
	phba->cfg_use_adisc = lpfc_use_adisc;
	phba->cfg_ack0 = lpfc_ack0;
	phba->cfg_topology = lpfc_topology;
	phba->cfg_scan_down = lpfc_scan_down;
	phba->cfg_nodev_tmo = lpfc_nodev_tmo;
	phba->cfg_link_speed = lpfc_link_speed;
	phba->cfg_fdmi_on = lpfc_fdmi_on;
	phba->cfg_discovery_threads = lpfc_discovery_threads;
	phba->cfg_max_luns = lpfc_max_luns;

	/*
	 * The total number of segments is the configuration value plus 2
	 * since the IOCB need a command and response bde.
	 */
	phba->cfg_sg_seg_cnt = LPFC_SG_SEG_CNT + 2;

	/*
	 * Since the sg_tablesize is module parameter, the sg_dma_buf_size
	 * used to create the sg_dma_buf_pool must be dynamically calculated
	 */
	phba->cfg_sg_dma_buf_size = sizeof(struct fcp_cmnd) +
			sizeof(struct fcp_rsp) +
			(phba->cfg_sg_seg_cnt * sizeof(struct ulp_bde64));

	switch (phba->pcidev->device) {
	case PCI_DEVICE_ID_LP101:
	case PCI_DEVICE_ID_BSMB:
	case PCI_DEVICE_ID_ZSMB:
		phba->cfg_hba_queue_depth = LPFC_LP101_HBA_Q_DEPTH;
		break;
	case PCI_DEVICE_ID_RFLY:
	case PCI_DEVICE_ID_PFLY:
	case PCI_DEVICE_ID_BMID:
	case PCI_DEVICE_ID_ZMID:
	case PCI_DEVICE_ID_TFLY:
		phba->cfg_hba_queue_depth = LPFC_LC_HBA_Q_DEPTH;
		break;
	default:
		phba->cfg_hba_queue_depth = LPFC_DFT_HBA_Q_DEPTH;
	}
	return;
}
