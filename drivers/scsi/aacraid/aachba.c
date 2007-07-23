/*
 *	Adaptec AAC series RAID controller driver
 *	(c) Copyright 2001 Red Hat Inc.	<alan@redhat.com>
 *
 * based on the old aacraid driver that is..
 * Adaptec aacraid device driver for Linux.
 *
 * Copyright (c) 2000-2007 Adaptec, Inc. (aacraid@adaptec.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/completion.h>
#include <linux/blkdev.h>
#include <linux/dma-mapping.h>
#include <asm/semaphore.h>
#include <asm/uaccess.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>

#include "aacraid.h"

/* values for inqd_pdt: Peripheral device type in plain English */
#define	INQD_PDT_DA	0x00	/* Direct-access (DISK) device */
#define	INQD_PDT_PROC	0x03	/* Processor device */
#define	INQD_PDT_CHNGR	0x08	/* Changer (jukebox, scsi2) */
#define	INQD_PDT_COMM	0x09	/* Communication device (scsi2) */
#define	INQD_PDT_NOLUN2 0x1f	/* Unknown Device (scsi2) */
#define	INQD_PDT_NOLUN	0x7f	/* Logical Unit Not Present */

#define	INQD_PDT_DMASK	0x1F	/* Peripheral Device Type Mask */
#define	INQD_PDT_QMASK	0xE0	/* Peripheral Device Qualifer Mask */

/*
 *	Sense codes
 */
 
#define SENCODE_NO_SENSE                        0x00
#define SENCODE_END_OF_DATA                     0x00
#define SENCODE_BECOMING_READY                  0x04
#define SENCODE_INIT_CMD_REQUIRED               0x04
#define SENCODE_PARAM_LIST_LENGTH_ERROR         0x1A
#define SENCODE_INVALID_COMMAND                 0x20
#define SENCODE_LBA_OUT_OF_RANGE                0x21
#define SENCODE_INVALID_CDB_FIELD               0x24
#define SENCODE_LUN_NOT_SUPPORTED               0x25
#define SENCODE_INVALID_PARAM_FIELD             0x26
#define SENCODE_PARAM_NOT_SUPPORTED             0x26
#define SENCODE_PARAM_VALUE_INVALID             0x26
#define SENCODE_RESET_OCCURRED                  0x29
#define SENCODE_LUN_NOT_SELF_CONFIGURED_YET     0x3E
#define SENCODE_INQUIRY_DATA_CHANGED            0x3F
#define SENCODE_SAVING_PARAMS_NOT_SUPPORTED     0x39
#define SENCODE_DIAGNOSTIC_FAILURE              0x40
#define SENCODE_INTERNAL_TARGET_FAILURE         0x44
#define SENCODE_INVALID_MESSAGE_ERROR           0x49
#define SENCODE_LUN_FAILED_SELF_CONFIG          0x4c
#define SENCODE_OVERLAPPED_COMMAND              0x4E

/*
 *	Additional sense codes
 */
 
#define ASENCODE_NO_SENSE                       0x00
#define ASENCODE_END_OF_DATA                    0x05
#define ASENCODE_BECOMING_READY                 0x01
#define ASENCODE_INIT_CMD_REQUIRED              0x02
#define ASENCODE_PARAM_LIST_LENGTH_ERROR        0x00
#define ASENCODE_INVALID_COMMAND                0x00
#define ASENCODE_LBA_OUT_OF_RANGE               0x00
#define ASENCODE_INVALID_CDB_FIELD              0x00
#define ASENCODE_LUN_NOT_SUPPORTED              0x00
#define ASENCODE_INVALID_PARAM_FIELD            0x00
#define ASENCODE_PARAM_NOT_SUPPORTED            0x01
#define ASENCODE_PARAM_VALUE_INVALID            0x02
#define ASENCODE_RESET_OCCURRED                 0x00
#define ASENCODE_LUN_NOT_SELF_CONFIGURED_YET    0x00
#define ASENCODE_INQUIRY_DATA_CHANGED           0x03
#define ASENCODE_SAVING_PARAMS_NOT_SUPPORTED    0x00
#define ASENCODE_DIAGNOSTIC_FAILURE             0x80
#define ASENCODE_INTERNAL_TARGET_FAILURE        0x00
#define ASENCODE_INVALID_MESSAGE_ERROR          0x00
#define ASENCODE_LUN_FAILED_SELF_CONFIG         0x00
#define ASENCODE_OVERLAPPED_COMMAND             0x00

#define BYTE0(x) (unsigned char)(x)
#define BYTE1(x) (unsigned char)((x) >> 8)
#define BYTE2(x) (unsigned char)((x) >> 16)
#define BYTE3(x) (unsigned char)((x) >> 24)

/*------------------------------------------------------------------------------
 *              S T R U C T S / T Y P E D E F S
 *----------------------------------------------------------------------------*/
/* SCSI inquiry data */
struct inquiry_data {
	u8 inqd_pdt;	/* Peripheral qualifier | Peripheral Device Type  */
	u8 inqd_dtq;	/* RMB | Device Type Qualifier  */
	u8 inqd_ver;	/* ISO version | ECMA version | ANSI-approved version */
	u8 inqd_rdf;	/* AENC | TrmIOP | Response data format */
	u8 inqd_len;	/* Additional length (n-4) */
	u8 inqd_pad1[2];/* Reserved - must be zero */
	u8 inqd_pad2;	/* RelAdr | WBus32 | WBus16 |  Sync  | Linked |Reserved| CmdQue | SftRe */
	u8 inqd_vid[8];	/* Vendor ID */
	u8 inqd_pid[16];/* Product ID */
	u8 inqd_prl[4];	/* Product Revision Level */
};

/*
 *              M O D U L E   G L O B A L S
 */
 
static unsigned long aac_build_sg(struct scsi_cmnd* scsicmd, struct sgmap* sgmap);
static unsigned long aac_build_sg64(struct scsi_cmnd* scsicmd, struct sgmap64* psg);
static unsigned long aac_build_sgraw(struct scsi_cmnd* scsicmd, struct sgmapraw* psg);
static int aac_send_srb_fib(struct scsi_cmnd* scsicmd);
#ifdef AAC_DETAILED_STATUS_INFO
static char *aac_get_status_string(u32 status);
#endif

/*
 *	Non dasd selection is handled entirely in aachba now
 */	
 
static int nondasd = -1;
static int dacmode = -1;

int aac_commit = -1;
int startup_timeout = 180;
int aif_timeout = 120;

module_param(nondasd, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(nondasd, "Control scanning of hba for nondasd devices. 0=off, 1=on");
module_param(dacmode, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(dacmode, "Control whether dma addressing is using 64 bit DAC. 0=off, 1=on");
module_param_named(commit, aac_commit, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(commit, "Control whether a COMMIT_CONFIG is issued to the adapter for foreign arrays.\nThis is typically needed in systems that do not have a BIOS. 0=off, 1=on");
module_param(startup_timeout, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(startup_timeout, "The duration of time in seconds to wait for adapter to have it's kernel up and\nrunning. This is typically adjusted for large systems that do not have a BIOS.");
module_param(aif_timeout, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(aif_timeout, "The duration of time in seconds to wait for applications to pick up AIFs before\nderegistering them. This is typically adjusted for heavily burdened systems.");

int numacb = -1;
module_param(numacb, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(numacb, "Request a limit to the number of adapter control blocks (FIB) allocated. Valid values are 512 and down. Default is to use suggestion from Firmware.");

int acbsize = -1;
module_param(acbsize, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(acbsize, "Request a specific adapter control block (FIB) size. Valid values are 512, 2048, 4096 and 8192. Default is to use suggestion from Firmware.");

int update_interval = 30 * 60;
module_param(update_interval, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(update_interval, "Interval in seconds between time sync updates issued to adapter.");

int check_interval = 24 * 60 * 60;
module_param(check_interval, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(check_interval, "Interval in seconds between adapter health checks.");

int check_reset = 1;
module_param(check_reset, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(check_reset, "If adapter fails health check, reset the adapter.");

int expose_physicals = -1;
module_param(expose_physicals, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(expose_physicals, "Expose physical components of the arrays. -1=protect 0=off, 1=on");

int aac_reset_devices = 0;
module_param_named(reset_devices, aac_reset_devices, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(reset_devices, "Force an adapter reset at initialization.");

static inline int aac_valid_context(struct scsi_cmnd *scsicmd,
		struct fib *fibptr) {
	struct scsi_device *device;

	if (unlikely(!scsicmd || !scsicmd->scsi_done )) {
		dprintk((KERN_WARNING "aac_valid_context: scsi command corrupt\n"))
;
                aac_fib_complete(fibptr);
                aac_fib_free(fibptr);
                return 0;
        }
	scsicmd->SCp.phase = AAC_OWNER_MIDLEVEL;
	device = scsicmd->device;
	if (unlikely(!device || !scsi_device_online(device))) {
		dprintk((KERN_WARNING "aac_valid_context: scsi device corrupt\n"));
		aac_fib_complete(fibptr);
		aac_fib_free(fibptr);
		return 0;
	}
	return 1;
}

/**
 *	aac_get_config_status	-	check the adapter configuration
 *	@common: adapter to query
 *
 *	Query config status, and commit the configuration if needed.
 */
int aac_get_config_status(struct aac_dev *dev, int commit_flag)
{
	int status = 0;
	struct fib * fibptr;

	if (!(fibptr = aac_fib_alloc(dev)))
		return -ENOMEM;

	aac_fib_init(fibptr);
	{
		struct aac_get_config_status *dinfo;
		dinfo = (struct aac_get_config_status *) fib_data(fibptr);

		dinfo->command = cpu_to_le32(VM_ContainerConfig);
		dinfo->type = cpu_to_le32(CT_GET_CONFIG_STATUS);
		dinfo->count = cpu_to_le32(sizeof(((struct aac_get_config_status_resp *)NULL)->data));
	}

	status = aac_fib_send(ContainerCommand,
			    fibptr,
			    sizeof (struct aac_get_config_status),
			    FsaNormal,
			    1, 1,
			    NULL, NULL);
	if (status < 0 ) {
		printk(KERN_WARNING "aac_get_config_status: SendFIB failed.\n");
	} else {
		struct aac_get_config_status_resp *reply
		  = (struct aac_get_config_status_resp *) fib_data(fibptr);
		dprintk((KERN_WARNING
		  "aac_get_config_status: response=%d status=%d action=%d\n",
		  le32_to_cpu(reply->response),
		  le32_to_cpu(reply->status),
		  le32_to_cpu(reply->data.action)));
		if ((le32_to_cpu(reply->response) != ST_OK) ||
		     (le32_to_cpu(reply->status) != CT_OK) ||
		     (le32_to_cpu(reply->data.action) > CFACT_PAUSE)) {
			printk(KERN_WARNING "aac_get_config_status: Will not issue the Commit Configuration\n");
			status = -EINVAL;
		}
	}
	aac_fib_complete(fibptr);
	/* Send a CT_COMMIT_CONFIG to enable discovery of devices */
	if (status >= 0) {
		if ((aac_commit == 1) || commit_flag) {
			struct aac_commit_config * dinfo;
			aac_fib_init(fibptr);
			dinfo = (struct aac_commit_config *) fib_data(fibptr);
	
			dinfo->command = cpu_to_le32(VM_ContainerConfig);
			dinfo->type = cpu_to_le32(CT_COMMIT_CONFIG);
	
			status = aac_fib_send(ContainerCommand,
				    fibptr,
				    sizeof (struct aac_commit_config),
				    FsaNormal,
				    1, 1,
				    NULL, NULL);
			aac_fib_complete(fibptr);
		} else if (aac_commit == 0) {
			printk(KERN_WARNING
			  "aac_get_config_status: Foreign device configurations are being ignored\n");
		}
	}
	aac_fib_free(fibptr);
	return status;
}

/**
 *	aac_get_containers	-	list containers
 *	@common: adapter to probe
 *
 *	Make a list of all containers on this controller
 */
int aac_get_containers(struct aac_dev *dev)
{
	struct fsa_dev_info *fsa_dev_ptr;
	u32 index; 
	int status = 0;
	struct fib * fibptr;
	struct aac_get_container_count *dinfo;
	struct aac_get_container_count_resp *dresp;
	int maximum_num_containers = MAXIMUM_NUM_CONTAINERS;

	if (!(fibptr = aac_fib_alloc(dev)))
		return -ENOMEM;

	aac_fib_init(fibptr);
	dinfo = (struct aac_get_container_count *) fib_data(fibptr);
	dinfo->command = cpu_to_le32(VM_ContainerConfig);
	dinfo->type = cpu_to_le32(CT_GET_CONTAINER_COUNT);

	status = aac_fib_send(ContainerCommand,
		    fibptr,
		    sizeof (struct aac_get_container_count),
		    FsaNormal,
		    1, 1,
		    NULL, NULL);
	if (status >= 0) {
		dresp = (struct aac_get_container_count_resp *)fib_data(fibptr);
		maximum_num_containers = le32_to_cpu(dresp->ContainerSwitchEntries);
		aac_fib_complete(fibptr);
	}
	aac_fib_free(fibptr);

	if (maximum_num_containers < MAXIMUM_NUM_CONTAINERS)
		maximum_num_containers = MAXIMUM_NUM_CONTAINERS;
	fsa_dev_ptr = kzalloc(sizeof(*fsa_dev_ptr) * maximum_num_containers,
			GFP_KERNEL);
	if (!fsa_dev_ptr)
		return -ENOMEM;

	dev->fsa_dev = fsa_dev_ptr;
	dev->maximum_num_containers = maximum_num_containers;

	for (index = 0; index < dev->maximum_num_containers; ) {
		fsa_dev_ptr[index].devname[0] = '\0';

		status = aac_probe_container(dev, index);

		if (status < 0) {
			printk(KERN_WARNING "aac_get_containers: SendFIB failed.\n");
			break;
		}

		/*
		 *	If there are no more containers, then stop asking.
		 */
		if (++index >= status)
			break;
	}
	return status;
}

static void aac_internal_transfer(struct scsi_cmnd *scsicmd, void *data, unsigned int offset, unsigned int len)
{
	void *buf;
	int transfer_len;
	struct scatterlist *sg = scsi_sglist(scsicmd);

	buf = kmap_atomic(sg->page, KM_IRQ0) + sg->offset;
	transfer_len = min(sg->length, len + offset);

	transfer_len -= offset;
	if (buf && transfer_len > 0)
		memcpy(buf + offset, data, transfer_len);

	kunmap_atomic(buf - sg->offset, KM_IRQ0);

}

static void get_container_name_callback(void *context, struct fib * fibptr)
{
	struct aac_get_name_resp * get_name_reply;
	struct scsi_cmnd * scsicmd;

	scsicmd = (struct scsi_cmnd *) context;

	if (!aac_valid_context(scsicmd, fibptr))
		return;

	dprintk((KERN_DEBUG "get_container_name_callback[cpu %d]: t = %ld.\n", smp_processor_id(), jiffies));
	BUG_ON(fibptr == NULL);

	get_name_reply = (struct aac_get_name_resp *) fib_data(fibptr);
	/* Failure is irrelevant, using default value instead */
	if ((le32_to_cpu(get_name_reply->status) == CT_OK)
	 && (get_name_reply->data[0] != '\0')) {
		char *sp = get_name_reply->data;
		sp[sizeof(((struct aac_get_name_resp *)NULL)->data)-1] = '\0';
		while (*sp == ' ')
			++sp;
		if (*sp) {
			char d[sizeof(((struct inquiry_data *)NULL)->inqd_pid)];
			int count = sizeof(d);
			char *dp = d;
			do {
				*dp++ = (*sp) ? *sp++ : ' ';
			} while (--count > 0);
			aac_internal_transfer(scsicmd, d, 
			  offsetof(struct inquiry_data, inqd_pid), sizeof(d));
		}
	}

	scsicmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8 | SAM_STAT_GOOD;

	aac_fib_complete(fibptr);
	aac_fib_free(fibptr);
	scsicmd->scsi_done(scsicmd);
}

/**
 *	aac_get_container_name	-	get container name, none blocking.
 */
static int aac_get_container_name(struct scsi_cmnd * scsicmd)
{
	int status;
	struct aac_get_name *dinfo;
	struct fib * cmd_fibcontext;
	struct aac_dev * dev;

	dev = (struct aac_dev *)scsicmd->device->host->hostdata;

	if (!(cmd_fibcontext = aac_fib_alloc(dev)))
		return -ENOMEM;

	aac_fib_init(cmd_fibcontext);
	dinfo = (struct aac_get_name *) fib_data(cmd_fibcontext);

	dinfo->command = cpu_to_le32(VM_ContainerConfig);
	dinfo->type = cpu_to_le32(CT_READ_NAME);
	dinfo->cid = cpu_to_le32(scmd_id(scsicmd));
	dinfo->count = cpu_to_le32(sizeof(((struct aac_get_name_resp *)NULL)->data));

	status = aac_fib_send(ContainerCommand,
		  cmd_fibcontext, 
		  sizeof (struct aac_get_name),
		  FsaNormal, 
		  0, 1, 
		  (fib_callback) get_container_name_callback, 
		  (void *) scsicmd);
	
	/*
	 *	Check that the command queued to the controller
	 */
	if (status == -EINPROGRESS) {
		scsicmd->SCp.phase = AAC_OWNER_FIRMWARE;
		return 0;
	}
		
	printk(KERN_WARNING "aac_get_container_name: aac_fib_send failed with status: %d.\n", status);
	aac_fib_complete(cmd_fibcontext);
	aac_fib_free(cmd_fibcontext);
	return -1;
}

static int aac_probe_container_callback2(struct scsi_cmnd * scsicmd)
{
	struct fsa_dev_info *fsa_dev_ptr = ((struct aac_dev *)(scsicmd->device->host->hostdata))->fsa_dev;

	if ((fsa_dev_ptr[scmd_id(scsicmd)].valid & 1))
		return aac_scsi_cmd(scsicmd);

	scsicmd->result = DID_NO_CONNECT << 16;
	scsicmd->scsi_done(scsicmd);
	return 0;
}

static void _aac_probe_container2(void * context, struct fib * fibptr)
{
	struct fsa_dev_info *fsa_dev_ptr;
	int (*callback)(struct scsi_cmnd *);
	struct scsi_cmnd * scsicmd = (struct scsi_cmnd *)context;


	if (!aac_valid_context(scsicmd, fibptr))
		return;

	scsicmd->SCp.Status = 0;
	fsa_dev_ptr = fibptr->dev->fsa_dev;
	if (fsa_dev_ptr) {
		struct aac_mount * dresp = (struct aac_mount *) fib_data(fibptr);
		fsa_dev_ptr += scmd_id(scsicmd);

		if ((le32_to_cpu(dresp->status) == ST_OK) &&
		    (le32_to_cpu(dresp->mnt[0].vol) != CT_NONE) &&
		    (le32_to_cpu(dresp->mnt[0].state) != FSCS_HIDDEN)) {
			fsa_dev_ptr->valid = 1;
			fsa_dev_ptr->type = le32_to_cpu(dresp->mnt[0].vol);
			fsa_dev_ptr->size
			  = ((u64)le32_to_cpu(dresp->mnt[0].capacity)) +
			    (((u64)le32_to_cpu(dresp->mnt[0].capacityhigh)) << 32);
			fsa_dev_ptr->ro = ((le32_to_cpu(dresp->mnt[0].state) & FSCS_READONLY) != 0);
		}
		if ((fsa_dev_ptr->valid & 1) == 0)
			fsa_dev_ptr->valid = 0;
		scsicmd->SCp.Status = le32_to_cpu(dresp->count);
	}
	aac_fib_complete(fibptr);
	aac_fib_free(fibptr);
	callback = (int (*)(struct scsi_cmnd *))(scsicmd->SCp.ptr);
	scsicmd->SCp.ptr = NULL;
	(*callback)(scsicmd);
	return;
}

static void _aac_probe_container1(void * context, struct fib * fibptr)
{
	struct scsi_cmnd * scsicmd;
	struct aac_mount * dresp;
	struct aac_query_mount *dinfo;
	int status;

	dresp = (struct aac_mount *) fib_data(fibptr);
	dresp->mnt[0].capacityhigh = 0;
	if ((le32_to_cpu(dresp->status) != ST_OK) ||
	    (le32_to_cpu(dresp->mnt[0].vol) != CT_NONE)) {
		_aac_probe_container2(context, fibptr);
		return;
	}
	scsicmd = (struct scsi_cmnd *) context;

	if (!aac_valid_context(scsicmd, fibptr))
		return;

	aac_fib_init(fibptr);

	dinfo = (struct aac_query_mount *)fib_data(fibptr);

	dinfo->command = cpu_to_le32(VM_NameServe64);
	dinfo->count = cpu_to_le32(scmd_id(scsicmd));
	dinfo->type = cpu_to_le32(FT_FILESYS);

	status = aac_fib_send(ContainerCommand,
			  fibptr,
			  sizeof(struct aac_query_mount),
			  FsaNormal,
			  0, 1,
			  _aac_probe_container2,
			  (void *) scsicmd);
	/*
	 *	Check that the command queued to the controller
	 */
	if (status == -EINPROGRESS)
		scsicmd->SCp.phase = AAC_OWNER_FIRMWARE;
	else if (status < 0) {
		/* Inherit results from VM_NameServe, if any */
		dresp->status = cpu_to_le32(ST_OK);
		_aac_probe_container2(context, fibptr);
	}
}

static int _aac_probe_container(struct scsi_cmnd * scsicmd, int (*callback)(struct scsi_cmnd *))
{
	struct fib * fibptr;
	int status = -ENOMEM;

	if ((fibptr = aac_fib_alloc((struct aac_dev *)scsicmd->device->host->hostdata))) {
		struct aac_query_mount *dinfo;

		aac_fib_init(fibptr);

		dinfo = (struct aac_query_mount *)fib_data(fibptr);

		dinfo->command = cpu_to_le32(VM_NameServe);
		dinfo->count = cpu_to_le32(scmd_id(scsicmd));
		dinfo->type = cpu_to_le32(FT_FILESYS);
		scsicmd->SCp.ptr = (char *)callback;

		status = aac_fib_send(ContainerCommand,
			  fibptr,
			  sizeof(struct aac_query_mount),
			  FsaNormal,
			  0, 1,
			  _aac_probe_container1,
			  (void *) scsicmd);
		/*
		 *	Check that the command queued to the controller
		 */
		if (status == -EINPROGRESS) {
			scsicmd->SCp.phase = AAC_OWNER_FIRMWARE;
			return 0;
		}
		if (status < 0) {
			scsicmd->SCp.ptr = NULL;
			aac_fib_complete(fibptr);
			aac_fib_free(fibptr);
		}
	}
	if (status < 0) {
		struct fsa_dev_info *fsa_dev_ptr = ((struct aac_dev *)(scsicmd->device->host->hostdata))->fsa_dev;
		if (fsa_dev_ptr) {
			fsa_dev_ptr += scmd_id(scsicmd);
			if ((fsa_dev_ptr->valid & 1) == 0) {
				fsa_dev_ptr->valid = 0;
				return (*callback)(scsicmd);
			}
		}
	}
	return status;
}

/**
 *	aac_probe_container		-	query a logical volume
 *	@dev: device to query
 *	@cid: container identifier
 *
 *	Queries the controller about the given volume. The volume information
 *	is updated in the struct fsa_dev_info structure rather than returned.
 */
static int aac_probe_container_callback1(struct scsi_cmnd * scsicmd)
{
	scsicmd->device = NULL;
	return 0;
}

int aac_probe_container(struct aac_dev *dev, int cid)
{
	struct scsi_cmnd *scsicmd = kmalloc(sizeof(*scsicmd), GFP_KERNEL);
	struct scsi_device *scsidev = kmalloc(sizeof(*scsidev), GFP_KERNEL);
	int status;

	if (!scsicmd || !scsidev) {
		kfree(scsicmd);
		kfree(scsidev);
		return -ENOMEM;
	}
	scsicmd->list.next = NULL;
	scsicmd->scsi_done = (void (*)(struct scsi_cmnd*))aac_probe_container_callback1;

	scsicmd->device = scsidev;
	scsidev->sdev_state = 0;
	scsidev->id = cid;
	scsidev->host = dev->scsi_host_ptr;

	if (_aac_probe_container(scsicmd, aac_probe_container_callback1) == 0)
		while (scsicmd->device == scsidev)
			schedule();
	kfree(scsidev);
	status = scsicmd->SCp.Status;
	kfree(scsicmd);
	return status;
}

/* Local Structure to set SCSI inquiry data strings */
struct scsi_inq {
	char vid[8];         /* Vendor ID */
	char pid[16];        /* Product ID */
	char prl[4];         /* Product Revision Level */
};

/**
 *	InqStrCopy	-	string merge
 *	@a:	string to copy from
 *	@b:	string to copy to
 *
 * 	Copy a String from one location to another
 *	without copying \0
 */

static void inqstrcpy(char *a, char *b)
{

	while(*a != (char)0) 
		*b++ = *a++;
}

static char *container_types[] = {
        "None",
        "Volume",
        "Mirror",
        "Stripe",
        "RAID5",
        "SSRW",
        "SSRO",
        "Morph",
        "Legacy",
        "RAID4",
        "RAID10",             
        "RAID00",             
        "V-MIRRORS",          
        "PSEUDO R4",          
	"RAID50",
	"RAID5D",
	"RAID5D0",
	"RAID1E",
	"RAID6",
	"RAID60",
        "Unknown"
};



/* Function: setinqstr
 *
 * Arguments: [1] pointer to void [1] int
 *
 * Purpose: Sets SCSI inquiry data strings for vendor, product
 * and revision level. Allows strings to be set in platform dependant
 * files instead of in OS dependant driver source.
 */

static void setinqstr(struct aac_dev *dev, void *data, int tindex)
{
	struct scsi_inq *str;

	str = (struct scsi_inq *)(data); /* cast data to scsi inq block */
	memset(str, ' ', sizeof(*str));

	if (dev->supplement_adapter_info.AdapterTypeText[0]) {
		char * cp = dev->supplement_adapter_info.AdapterTypeText;
		int c = sizeof(str->vid);
		while (*cp && *cp != ' ' && --c)
			++cp;
		c = *cp;
		*cp = '\0';
		inqstrcpy (dev->supplement_adapter_info.AdapterTypeText,
		  str->vid); 
		*cp = c;
		while (*cp && *cp != ' ')
			++cp;
		while (*cp == ' ')
			++cp;
		/* last six chars reserved for vol type */
		c = 0;
		if (strlen(cp) > sizeof(str->pid)) {
			c = cp[sizeof(str->pid)];
			cp[sizeof(str->pid)] = '\0';
		}
		inqstrcpy (cp, str->pid);
		if (c)
			cp[sizeof(str->pid)] = c;
	} else {
		struct aac_driver_ident *mp = aac_get_driver_ident(dev->cardtype);

		inqstrcpy (mp->vname, str->vid);
		/* last six chars reserved for vol type */
		inqstrcpy (mp->model, str->pid);
	}

	if (tindex < ARRAY_SIZE(container_types)){
		char *findit = str->pid;

		for ( ; *findit != ' '; findit++); /* walk till we find a space */
		/* RAID is superfluous in the context of a RAID device */
		if (memcmp(findit-4, "RAID", 4) == 0)
			*(findit -= 4) = ' ';
		if (((findit - str->pid) + strlen(container_types[tindex]))
		 < (sizeof(str->pid) + sizeof(str->prl)))
			inqstrcpy (container_types[tindex], findit + 1);
	}
	inqstrcpy ("V1.0", str->prl);
}

static void get_container_serial_callback(void *context, struct fib * fibptr)
{
	struct aac_get_serial_resp * get_serial_reply;
	struct scsi_cmnd * scsicmd;

	BUG_ON(fibptr == NULL);

	scsicmd = (struct scsi_cmnd *) context;
	if (!aac_valid_context(scsicmd, fibptr))
		return;

	get_serial_reply = (struct aac_get_serial_resp *) fib_data(fibptr);
	/* Failure is irrelevant, using default value instead */
	if (le32_to_cpu(get_serial_reply->status) == CT_OK) {
		char sp[13];
		/* EVPD bit set */
		sp[0] = INQD_PDT_DA;
		sp[1] = scsicmd->cmnd[2];
		sp[2] = 0;
		sp[3] = snprintf(sp+4, sizeof(sp)-4, "%08X",
		  le32_to_cpu(get_serial_reply->uid));
		aac_internal_transfer(scsicmd, sp, 0, sizeof(sp));
	}

	scsicmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8 | SAM_STAT_GOOD;

	aac_fib_complete(fibptr);
	aac_fib_free(fibptr);
	scsicmd->scsi_done(scsicmd);
}

/**
 *	aac_get_container_serial - get container serial, none blocking.
 */
static int aac_get_container_serial(struct scsi_cmnd * scsicmd)
{
	int status;
	struct aac_get_serial *dinfo;
	struct fib * cmd_fibcontext;
	struct aac_dev * dev;

	dev = (struct aac_dev *)scsicmd->device->host->hostdata;

	if (!(cmd_fibcontext = aac_fib_alloc(dev)))
		return -ENOMEM;

	aac_fib_init(cmd_fibcontext);
	dinfo = (struct aac_get_serial *) fib_data(cmd_fibcontext);

	dinfo->command = cpu_to_le32(VM_ContainerConfig);
	dinfo->type = cpu_to_le32(CT_CID_TO_32BITS_UID);
	dinfo->cid = cpu_to_le32(scmd_id(scsicmd));

	status = aac_fib_send(ContainerCommand,
		  cmd_fibcontext,
		  sizeof (struct aac_get_serial),
		  FsaNormal,
		  0, 1,
		  (fib_callback) get_container_serial_callback,
		  (void *) scsicmd);

	/*
	 *	Check that the command queued to the controller
	 */
	if (status == -EINPROGRESS) {
		scsicmd->SCp.phase = AAC_OWNER_FIRMWARE;
		return 0;
	}

	printk(KERN_WARNING "aac_get_container_serial: aac_fib_send failed with status: %d.\n", status);
	aac_fib_complete(cmd_fibcontext);
	aac_fib_free(cmd_fibcontext);
	return -1;
}

/* Function: setinqserial
 *
 * Arguments: [1] pointer to void [1] int
 *
 * Purpose: Sets SCSI Unit Serial number.
 *          This is a fake. We should read a proper
 *          serial number from the container. <SuSE>But
 *          without docs it's quite hard to do it :-)
 *          So this will have to do in the meantime.</SuSE>
 */

static int setinqserial(struct aac_dev *dev, void *data, int cid)
{
	/*
	 *	This breaks array migration.
	 */
	return snprintf((char *)(data), sizeof(struct scsi_inq) - 4, "%08X%02X",
			le32_to_cpu(dev->adapter_info.serial[0]), cid);
}

static void set_sense(u8 *sense_buf, u8 sense_key, u8 sense_code,
		      u8 a_sense_code, u8 incorrect_length,
		      u8 bit_pointer, u16 field_pointer,
		      u32 residue)
{
	sense_buf[0] = 0xF0;	/* Sense data valid, err code 70h (current error) */
	sense_buf[1] = 0;	/* Segment number, always zero */

	if (incorrect_length) {
		sense_buf[2] = sense_key | 0x20;/* Set ILI bit | sense key */
		sense_buf[3] = BYTE3(residue);
		sense_buf[4] = BYTE2(residue);
		sense_buf[5] = BYTE1(residue);
		sense_buf[6] = BYTE0(residue);
	} else
		sense_buf[2] = sense_key;	/* Sense key */

	if (sense_key == ILLEGAL_REQUEST)
		sense_buf[7] = 10;	/* Additional sense length */
	else
		sense_buf[7] = 6;	/* Additional sense length */

	sense_buf[12] = sense_code;	/* Additional sense code */
	sense_buf[13] = a_sense_code;	/* Additional sense code qualifier */
	if (sense_key == ILLEGAL_REQUEST) {
		sense_buf[15] = 0;

		if (sense_code == SENCODE_INVALID_PARAM_FIELD)
			sense_buf[15] = 0x80;/* Std sense key specific field */
		/* Illegal parameter is in the parameter block */

		if (sense_code == SENCODE_INVALID_CDB_FIELD)
			sense_buf[15] = 0xc0;/* Std sense key specific field */
		/* Illegal parameter is in the CDB block */
		sense_buf[15] |= bit_pointer;
		sense_buf[16] = field_pointer >> 8;	/* MSB */
		sense_buf[17] = field_pointer;		/* LSB */
	}
}

static int aac_bounds_32(struct aac_dev * dev, struct scsi_cmnd * cmd, u64 lba)
{
	if (lba & 0xffffffff00000000LL) {
		int cid = scmd_id(cmd);
		dprintk((KERN_DEBUG "aacraid: Illegal lba\n"));
		cmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8 |
			SAM_STAT_CHECK_CONDITION;
		set_sense((u8 *) &dev->fsa_dev[cid].sense_data,
			    HARDWARE_ERROR,
			    SENCODE_INTERNAL_TARGET_FAILURE,
			    ASENCODE_INTERNAL_TARGET_FAILURE, 0, 0,
			    0, 0);
		memcpy(cmd->sense_buffer, &dev->fsa_dev[cid].sense_data,
		  (sizeof(dev->fsa_dev[cid].sense_data) > sizeof(cmd->sense_buffer))
		    ? sizeof(cmd->sense_buffer)
		    : sizeof(dev->fsa_dev[cid].sense_data));
		cmd->scsi_done(cmd);
		return 1;
	}
	return 0;
}

static int aac_bounds_64(struct aac_dev * dev, struct scsi_cmnd * cmd, u64 lba)
{
	return 0;
}

static void io_callback(void *context, struct fib * fibptr);

static int aac_read_raw_io(struct fib * fib, struct scsi_cmnd * cmd, u64 lba, u32 count)
{
	u16 fibsize;
	struct aac_raw_io *readcmd;
	aac_fib_init(fib);
	readcmd = (struct aac_raw_io *) fib_data(fib);
	readcmd->block[0] = cpu_to_le32((u32)(lba&0xffffffff));
	readcmd->block[1] = cpu_to_le32((u32)((lba&0xffffffff00000000LL)>>32));
	readcmd->count = cpu_to_le32(count<<9);
	readcmd->cid = cpu_to_le16(scmd_id(cmd));
	readcmd->flags = cpu_to_le16(IO_TYPE_READ);
	readcmd->bpTotal = 0;
	readcmd->bpComplete = 0;

	aac_build_sgraw(cmd, &readcmd->sg);
	fibsize = sizeof(struct aac_raw_io) + ((le32_to_cpu(readcmd->sg.count) - 1) * sizeof (struct sgentryraw));
	BUG_ON(fibsize > (fib->dev->max_fib_size - sizeof(struct aac_fibhdr)));
	/*
	 *	Now send the Fib to the adapter
	 */
	return aac_fib_send(ContainerRawIo,
			  fib,
			  fibsize,
			  FsaNormal,
			  0, 1,
			  (fib_callback) io_callback,
			  (void *) cmd);
}

static int aac_read_block64(struct fib * fib, struct scsi_cmnd * cmd, u64 lba, u32 count)
{
	u16 fibsize;
	struct aac_read64 *readcmd;
	aac_fib_init(fib);
	readcmd = (struct aac_read64 *) fib_data(fib);
	readcmd->command = cpu_to_le32(VM_CtHostRead64);
	readcmd->cid = cpu_to_le16(scmd_id(cmd));
	readcmd->sector_count = cpu_to_le16(count);
	readcmd->block = cpu_to_le32((u32)(lba&0xffffffff));
	readcmd->pad   = 0;
	readcmd->flags = 0;

	aac_build_sg64(cmd, &readcmd->sg);
	fibsize = sizeof(struct aac_read64) +
		((le32_to_cpu(readcmd->sg.count) - 1) *
		 sizeof (struct sgentry64));
	BUG_ON (fibsize > (fib->dev->max_fib_size -
				sizeof(struct aac_fibhdr)));
	/*
	 *	Now send the Fib to the adapter
	 */
	return aac_fib_send(ContainerCommand64,
			  fib,
			  fibsize,
			  FsaNormal,
			  0, 1,
			  (fib_callback) io_callback,
			  (void *) cmd);
}

static int aac_read_block(struct fib * fib, struct scsi_cmnd * cmd, u64 lba, u32 count)
{
	u16 fibsize;
	struct aac_read *readcmd;
	aac_fib_init(fib);
	readcmd = (struct aac_read *) fib_data(fib);
	readcmd->command = cpu_to_le32(VM_CtBlockRead);
	readcmd->cid = cpu_to_le16(scmd_id(cmd));
	readcmd->block = cpu_to_le32((u32)(lba&0xffffffff));
	readcmd->count = cpu_to_le32(count * 512);

	aac_build_sg(cmd, &readcmd->sg);
	fibsize = sizeof(struct aac_read) +
			((le32_to_cpu(readcmd->sg.count) - 1) *
			 sizeof (struct sgentry));
	BUG_ON (fibsize > (fib->dev->max_fib_size -
				sizeof(struct aac_fibhdr)));
	/*
	 *	Now send the Fib to the adapter
	 */
	return aac_fib_send(ContainerCommand,
			  fib,
			  fibsize,
			  FsaNormal,
			  0, 1,
			  (fib_callback) io_callback,
			  (void *) cmd);
}

static int aac_write_raw_io(struct fib * fib, struct scsi_cmnd * cmd, u64 lba, u32 count, int fua)
{
	u16 fibsize;
	struct aac_raw_io *writecmd;
	aac_fib_init(fib);
	writecmd = (struct aac_raw_io *) fib_data(fib);
	writecmd->block[0] = cpu_to_le32((u32)(lba&0xffffffff));
	writecmd->block[1] = cpu_to_le32((u32)((lba&0xffffffff00000000LL)>>32));
	writecmd->count = cpu_to_le32(count<<9);
	writecmd->cid = cpu_to_le16(scmd_id(cmd));
	writecmd->flags = fua ?
		cpu_to_le16(IO_TYPE_WRITE|IO_SUREWRITE) :
		cpu_to_le16(IO_TYPE_WRITE);
	writecmd->bpTotal = 0;
	writecmd->bpComplete = 0;

	aac_build_sgraw(cmd, &writecmd->sg);
	fibsize = sizeof(struct aac_raw_io) + ((le32_to_cpu(writecmd->sg.count) - 1) * sizeof (struct sgentryraw));
	BUG_ON(fibsize > (fib->dev->max_fib_size - sizeof(struct aac_fibhdr)));
	/*
	 *	Now send the Fib to the adapter
	 */
	return aac_fib_send(ContainerRawIo,
			  fib,
			  fibsize,
			  FsaNormal,
			  0, 1,
			  (fib_callback) io_callback,
			  (void *) cmd);
}

static int aac_write_block64(struct fib * fib, struct scsi_cmnd * cmd, u64 lba, u32 count, int fua)
{
	u16 fibsize;
	struct aac_write64 *writecmd;
	aac_fib_init(fib);
	writecmd = (struct aac_write64 *) fib_data(fib);
	writecmd->command = cpu_to_le32(VM_CtHostWrite64);
	writecmd->cid = cpu_to_le16(scmd_id(cmd));
	writecmd->sector_count = cpu_to_le16(count);
	writecmd->block = cpu_to_le32((u32)(lba&0xffffffff));
	writecmd->pad	= 0;
	writecmd->flags	= 0;

	aac_build_sg64(cmd, &writecmd->sg);
	fibsize = sizeof(struct aac_write64) +
		((le32_to_cpu(writecmd->sg.count) - 1) *
		 sizeof (struct sgentry64));
	BUG_ON (fibsize > (fib->dev->max_fib_size -
				sizeof(struct aac_fibhdr)));
	/*
	 *	Now send the Fib to the adapter
	 */
	return aac_fib_send(ContainerCommand64,
			  fib,
			  fibsize,
			  FsaNormal,
			  0, 1,
			  (fib_callback) io_callback,
			  (void *) cmd);
}

static int aac_write_block(struct fib * fib, struct scsi_cmnd * cmd, u64 lba, u32 count, int fua)
{
	u16 fibsize;
	struct aac_write *writecmd;
	aac_fib_init(fib);
	writecmd = (struct aac_write *) fib_data(fib);
	writecmd->command = cpu_to_le32(VM_CtBlockWrite);
	writecmd->cid = cpu_to_le16(scmd_id(cmd));
	writecmd->block = cpu_to_le32((u32)(lba&0xffffffff));
	writecmd->count = cpu_to_le32(count * 512);
	writecmd->sg.count = cpu_to_le32(1);
	/* ->stable is not used - it did mean which type of write */

	aac_build_sg(cmd, &writecmd->sg);
	fibsize = sizeof(struct aac_write) +
		((le32_to_cpu(writecmd->sg.count) - 1) *
		 sizeof (struct sgentry));
	BUG_ON (fibsize > (fib->dev->max_fib_size -
				sizeof(struct aac_fibhdr)));
	/*
	 *	Now send the Fib to the adapter
	 */
	return aac_fib_send(ContainerCommand,
			  fib,
			  fibsize,
			  FsaNormal,
			  0, 1,
			  (fib_callback) io_callback,
			  (void *) cmd);
}

static struct aac_srb * aac_scsi_common(struct fib * fib, struct scsi_cmnd * cmd)
{
	struct aac_srb * srbcmd;
	u32 flag;
	u32 timeout;

	aac_fib_init(fib);
	switch(cmd->sc_data_direction){
	case DMA_TO_DEVICE:
		flag = SRB_DataOut;
		break;
	case DMA_BIDIRECTIONAL:
		flag = SRB_DataIn | SRB_DataOut;
		break;
	case DMA_FROM_DEVICE:
		flag = SRB_DataIn;
		break;
	case DMA_NONE:
	default:	/* shuts up some versions of gcc */
		flag = SRB_NoDataXfer;
		break;
	}

	srbcmd = (struct aac_srb*) fib_data(fib);
	srbcmd->function = cpu_to_le32(SRBF_ExecuteScsi);
	srbcmd->channel  = cpu_to_le32(aac_logical_to_phys(scmd_channel(cmd)));
	srbcmd->id       = cpu_to_le32(scmd_id(cmd));
	srbcmd->lun      = cpu_to_le32(cmd->device->lun);
	srbcmd->flags    = cpu_to_le32(flag);
	timeout = cmd->timeout_per_command/HZ;
	if (timeout == 0)
		timeout = 1;
	srbcmd->timeout  = cpu_to_le32(timeout);  // timeout in seconds
	srbcmd->retry_limit = 0; /* Obsolete parameter */
	srbcmd->cdb_size = cpu_to_le32(cmd->cmd_len);
	return srbcmd;
}

static void aac_srb_callback(void *context, struct fib * fibptr);

static int aac_scsi_64(struct fib * fib, struct scsi_cmnd * cmd)
{
	u16 fibsize;
	struct aac_srb * srbcmd = aac_scsi_common(fib, cmd);

	aac_build_sg64(cmd, (struct sgmap64*) &srbcmd->sg);
	srbcmd->count = cpu_to_le32(scsi_bufflen(cmd));

	memset(srbcmd->cdb, 0, sizeof(srbcmd->cdb));
	memcpy(srbcmd->cdb, cmd->cmnd, cmd->cmd_len);
	/*
	 *	Build Scatter/Gather list
	 */
	fibsize = sizeof (struct aac_srb) - sizeof (struct sgentry) +
		((le32_to_cpu(srbcmd->sg.count) & 0xff) *
		 sizeof (struct sgentry64));
	BUG_ON (fibsize > (fib->dev->max_fib_size -
				sizeof(struct aac_fibhdr)));

	/*
	 *	Now send the Fib to the adapter
	 */
	return aac_fib_send(ScsiPortCommand64, fib,
				fibsize, FsaNormal, 0, 1,
				  (fib_callback) aac_srb_callback,
				  (void *) cmd);
}

static int aac_scsi_32(struct fib * fib, struct scsi_cmnd * cmd)
{
	u16 fibsize;
	struct aac_srb * srbcmd = aac_scsi_common(fib, cmd);

	aac_build_sg(cmd, (struct sgmap*)&srbcmd->sg);
	srbcmd->count = cpu_to_le32(scsi_bufflen(cmd));

	memset(srbcmd->cdb, 0, sizeof(srbcmd->cdb));
	memcpy(srbcmd->cdb, cmd->cmnd, cmd->cmd_len);
	/*
	 *	Build Scatter/Gather list
	 */
	fibsize = sizeof (struct aac_srb) +
		(((le32_to_cpu(srbcmd->sg.count) & 0xff) - 1) *
		 sizeof (struct sgentry));
	BUG_ON (fibsize > (fib->dev->max_fib_size -
				sizeof(struct aac_fibhdr)));

	/*
	 *	Now send the Fib to the adapter
	 */
	return aac_fib_send(ScsiPortCommand, fib, fibsize, FsaNormal, 0, 1,
				  (fib_callback) aac_srb_callback, (void *) cmd);
}

int aac_get_adapter_info(struct aac_dev* dev)
{
	struct fib* fibptr;
	int rcode;
	u32 tmp;
	struct aac_adapter_info *info;
	struct aac_bus_info *command;
	struct aac_bus_info_response *bus_info;

	if (!(fibptr = aac_fib_alloc(dev)))
		return -ENOMEM;

	aac_fib_init(fibptr);
	info = (struct aac_adapter_info *) fib_data(fibptr);
	memset(info,0,sizeof(*info));

	rcode = aac_fib_send(RequestAdapterInfo,
			 fibptr, 
			 sizeof(*info),
			 FsaNormal, 
			 -1, 1, /* First `interrupt' command uses special wait */
			 NULL, 
			 NULL);

	if (rcode < 0) {
		aac_fib_complete(fibptr);
		aac_fib_free(fibptr);
		return rcode;
	}
	memcpy(&dev->adapter_info, info, sizeof(*info));

	if (dev->adapter_info.options & AAC_OPT_SUPPLEMENT_ADAPTER_INFO) {
		struct aac_supplement_adapter_info * info;

		aac_fib_init(fibptr);

		info = (struct aac_supplement_adapter_info *) fib_data(fibptr);

		memset(info,0,sizeof(*info));

		rcode = aac_fib_send(RequestSupplementAdapterInfo,
				 fibptr,
				 sizeof(*info),
				 FsaNormal,
				 1, 1,
				 NULL,
				 NULL);

		if (rcode >= 0)
			memcpy(&dev->supplement_adapter_info, info, sizeof(*info));
	}


	/* 
	 * GetBusInfo 
	 */

	aac_fib_init(fibptr);

	bus_info = (struct aac_bus_info_response *) fib_data(fibptr);

	memset(bus_info, 0, sizeof(*bus_info));

	command = (struct aac_bus_info *)bus_info;

	command->Command = cpu_to_le32(VM_Ioctl);
	command->ObjType = cpu_to_le32(FT_DRIVE);
	command->MethodId = cpu_to_le32(1);
	command->CtlCmd = cpu_to_le32(GetBusInfo);

	rcode = aac_fib_send(ContainerCommand,
			 fibptr,
			 sizeof (*bus_info),
			 FsaNormal,
			 1, 1,
			 NULL, NULL);

	if (rcode >= 0 && le32_to_cpu(bus_info->Status) == ST_OK) {
		dev->maximum_num_physicals = le32_to_cpu(bus_info->TargetsPerBus);
		dev->maximum_num_channels = le32_to_cpu(bus_info->BusCount);
	}

	if (!dev->in_reset) {
		char buffer[16];
		tmp = le32_to_cpu(dev->adapter_info.kernelrev);
		printk(KERN_INFO "%s%d: kernel %d.%d-%d[%d] %.*s\n",
			dev->name, 
			dev->id,
			tmp>>24,
			(tmp>>16)&0xff,
			tmp&0xff,
			le32_to_cpu(dev->adapter_info.kernelbuild),
			(int)sizeof(dev->supplement_adapter_info.BuildDate),
			dev->supplement_adapter_info.BuildDate);
		tmp = le32_to_cpu(dev->adapter_info.monitorrev);
		printk(KERN_INFO "%s%d: monitor %d.%d-%d[%d]\n",
			dev->name, dev->id,
			tmp>>24,(tmp>>16)&0xff,tmp&0xff,
			le32_to_cpu(dev->adapter_info.monitorbuild));
		tmp = le32_to_cpu(dev->adapter_info.biosrev);
		printk(KERN_INFO "%s%d: bios %d.%d-%d[%d]\n",
			dev->name, dev->id,
			tmp>>24,(tmp>>16)&0xff,tmp&0xff,
			le32_to_cpu(dev->adapter_info.biosbuild));
		buffer[0] = '\0';
		if (aac_show_serial_number(
		  shost_to_class(dev->scsi_host_ptr), buffer))
			printk(KERN_INFO "%s%d: serial %s",
			  dev->name, dev->id, buffer);
		if (dev->supplement_adapter_info.VpdInfo.Tsid[0]) {
			printk(KERN_INFO "%s%d: TSID %.*s\n",
			  dev->name, dev->id,
			  (int)sizeof(dev->supplement_adapter_info.VpdInfo.Tsid),
			  dev->supplement_adapter_info.VpdInfo.Tsid);
		}
		if (!check_reset ||
		  (dev->supplement_adapter_info.SupportedOptions2 &
		  le32_to_cpu(AAC_OPTION_IGNORE_RESET))) {
			printk(KERN_INFO "%s%d: Reset Adapter Ignored\n",
			  dev->name, dev->id);
		}
	}

	dev->nondasd_support = 0;
	dev->raid_scsi_mode = 0;
	if(dev->adapter_info.options & AAC_OPT_NONDASD){
		dev->nondasd_support = 1;
	}

	/*
	 * If the firmware supports ROMB RAID/SCSI mode and we are currently
	 * in RAID/SCSI mode, set the flag. For now if in this mode we will
	 * force nondasd support on. If we decide to allow the non-dasd flag
	 * additional changes changes will have to be made to support
	 * RAID/SCSI.  the function aac_scsi_cmd in this module will have to be
	 * changed to support the new dev->raid_scsi_mode flag instead of
	 * leaching off of the dev->nondasd_support flag. Also in linit.c the
	 * function aac_detect will have to be modified where it sets up the
	 * max number of channels based on the aac->nondasd_support flag only.
	 */
	if ((dev->adapter_info.options & AAC_OPT_SCSI_MANAGED) &&
	    (dev->adapter_info.options & AAC_OPT_RAID_SCSI_MODE)) {
		dev->nondasd_support = 1;
		dev->raid_scsi_mode = 1;
	}
	if (dev->raid_scsi_mode != 0)
		printk(KERN_INFO "%s%d: ROMB RAID/SCSI mode enabled\n",
				dev->name, dev->id);
		
	if(nondasd != -1) {  
		dev->nondasd_support = (nondasd!=0);
	}
	if(dev->nondasd_support != 0){
		printk(KERN_INFO "%s%d: Non-DASD support enabled.\n",dev->name, dev->id);
	}

	dev->dac_support = 0;
	if( (sizeof(dma_addr_t) > 4) && (dev->adapter_info.options & AAC_OPT_SGMAP_HOST64)){
		printk(KERN_INFO "%s%d: 64bit support enabled.\n", dev->name, dev->id);
		dev->dac_support = 1;
	}

	if(dacmode != -1) {
		dev->dac_support = (dacmode!=0);
	}
	if(dev->dac_support != 0) {
		if (!pci_set_dma_mask(dev->pdev, DMA_64BIT_MASK) &&
			!pci_set_consistent_dma_mask(dev->pdev, DMA_64BIT_MASK)) {
			printk(KERN_INFO"%s%d: 64 Bit DAC enabled\n",
				dev->name, dev->id);
		} else if (!pci_set_dma_mask(dev->pdev, DMA_32BIT_MASK) &&
			!pci_set_consistent_dma_mask(dev->pdev, DMA_32BIT_MASK)) {
			printk(KERN_INFO"%s%d: DMA mask set failed, 64 Bit DAC disabled\n",
				dev->name, dev->id);
			dev->dac_support = 0;
		} else {
			printk(KERN_WARNING"%s%d: No suitable DMA available.\n",
				dev->name, dev->id);
			rcode = -ENOMEM;
		}
	}
	/* 
	 * Deal with configuring for the individualized limits of each packet
	 * interface.
	 */
	dev->a_ops.adapter_scsi = (dev->dac_support)
				? aac_scsi_64
				: aac_scsi_32;
	if (dev->raw_io_interface) {
		dev->a_ops.adapter_bounds = (dev->raw_io_64)
					? aac_bounds_64
					: aac_bounds_32;
		dev->a_ops.adapter_read = aac_read_raw_io;
		dev->a_ops.adapter_write = aac_write_raw_io;
	} else {
		dev->a_ops.adapter_bounds = aac_bounds_32;
		dev->scsi_host_ptr->sg_tablesize = (dev->max_fib_size -
			sizeof(struct aac_fibhdr) -
			sizeof(struct aac_write) + sizeof(struct sgentry)) /
				sizeof(struct sgentry);
		if (dev->dac_support) {
			dev->a_ops.adapter_read = aac_read_block64;
			dev->a_ops.adapter_write = aac_write_block64;
			/* 
			 * 38 scatter gather elements 
			 */
			dev->scsi_host_ptr->sg_tablesize =
				(dev->max_fib_size -
				sizeof(struct aac_fibhdr) -
				sizeof(struct aac_write64) +
				sizeof(struct sgentry64)) /
					sizeof(struct sgentry64);
		} else {
			dev->a_ops.adapter_read = aac_read_block;
			dev->a_ops.adapter_write = aac_write_block;
		}
		dev->scsi_host_ptr->max_sectors = AAC_MAX_32BIT_SGBCOUNT;
		if(!(dev->adapter_info.options & AAC_OPT_NEW_COMM)) {
			/*
			 * Worst case size that could cause sg overflow when
			 * we break up SG elements that are larger than 64KB.
			 * Would be nice if we could tell the SCSI layer what
			 * the maximum SG element size can be. Worst case is
			 * (sg_tablesize-1) 4KB elements with one 64KB
			 * element.
			 *	32bit -> 468 or 238KB	64bit -> 424 or 212KB
			 */
			dev->scsi_host_ptr->max_sectors =
			  (dev->scsi_host_ptr->sg_tablesize * 8) + 112;
		}
	}

	aac_fib_complete(fibptr);
	aac_fib_free(fibptr);

	return rcode;
}


static void io_callback(void *context, struct fib * fibptr)
{
	struct aac_dev *dev;
	struct aac_read_reply *readreply;
	struct scsi_cmnd *scsicmd;
	u32 cid;

	scsicmd = (struct scsi_cmnd *) context;

	if (!aac_valid_context(scsicmd, fibptr))
		return;

	dev = fibptr->dev;
	cid = scmd_id(scsicmd);

	if (nblank(dprintk(x))) {
		u64 lba;
		switch (scsicmd->cmnd[0]) {
		case WRITE_6:
		case READ_6:
			lba = ((scsicmd->cmnd[1] & 0x1F) << 16) |
			    (scsicmd->cmnd[2] << 8) | scsicmd->cmnd[3];
			break;
		case WRITE_16:
		case READ_16:
			lba = ((u64)scsicmd->cmnd[2] << 56) |
			      ((u64)scsicmd->cmnd[3] << 48) |
			      ((u64)scsicmd->cmnd[4] << 40) |
			      ((u64)scsicmd->cmnd[5] << 32) |
			      ((u64)scsicmd->cmnd[6] << 24) |
			      (scsicmd->cmnd[7] << 16) |
			      (scsicmd->cmnd[8] << 8) | scsicmd->cmnd[9];
			break;
		case WRITE_12:
		case READ_12:
			lba = ((u64)scsicmd->cmnd[2] << 24) |
			      (scsicmd->cmnd[3] << 16) |
			      (scsicmd->cmnd[4] << 8) | scsicmd->cmnd[5];
			break;
		default:
			lba = ((u64)scsicmd->cmnd[2] << 24) |
			       (scsicmd->cmnd[3] << 16) |
			       (scsicmd->cmnd[4] << 8) | scsicmd->cmnd[5];
			break;
		}
		printk(KERN_DEBUG
		  "io_callback[cpu %d]: lba = %llu, t = %ld.\n",
		  smp_processor_id(), (unsigned long long)lba, jiffies);
	}

	BUG_ON(fibptr == NULL);

	scsi_dma_unmap(scsicmd);

	readreply = (struct aac_read_reply *)fib_data(fibptr);
	if (le32_to_cpu(readreply->status) == ST_OK)
		scsicmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8 | SAM_STAT_GOOD;
	else {
#ifdef AAC_DETAILED_STATUS_INFO
		printk(KERN_WARNING "io_callback: io failed, status = %d\n",
		  le32_to_cpu(readreply->status));
#endif
		scsicmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8 | SAM_STAT_CHECK_CONDITION;
		set_sense((u8 *) &dev->fsa_dev[cid].sense_data,
				    HARDWARE_ERROR,
				    SENCODE_INTERNAL_TARGET_FAILURE,
				    ASENCODE_INTERNAL_TARGET_FAILURE, 0, 0,
				    0, 0);
		memcpy(scsicmd->sense_buffer, &dev->fsa_dev[cid].sense_data,
		  (sizeof(dev->fsa_dev[cid].sense_data) > sizeof(scsicmd->sense_buffer))
		    ? sizeof(scsicmd->sense_buffer)
		    : sizeof(dev->fsa_dev[cid].sense_data));
	}
	aac_fib_complete(fibptr);
	aac_fib_free(fibptr);

	scsicmd->scsi_done(scsicmd);
}

static int aac_read(struct scsi_cmnd * scsicmd)
{
	u64 lba;
	u32 count;
	int status;
	struct aac_dev *dev;
	struct fib * cmd_fibcontext;

	dev = (struct aac_dev *)scsicmd->device->host->hostdata;
	/*
	 *	Get block address and transfer length
	 */
	switch (scsicmd->cmnd[0]) {
	case READ_6:
		dprintk((KERN_DEBUG "aachba: received a read(6) command on id %d.\n", scmd_id(scsicmd)));

		lba = ((scsicmd->cmnd[1] & 0x1F) << 16) | 
			(scsicmd->cmnd[2] << 8) | scsicmd->cmnd[3];
		count = scsicmd->cmnd[4];

		if (count == 0)
			count = 256;
		break;
	case READ_16:
		dprintk((KERN_DEBUG "aachba: received a read(16) command on id %d.\n", scmd_id(scsicmd)));

		lba = 	((u64)scsicmd->cmnd[2] << 56) |
		 	((u64)scsicmd->cmnd[3] << 48) |
			((u64)scsicmd->cmnd[4] << 40) |
			((u64)scsicmd->cmnd[5] << 32) |
			((u64)scsicmd->cmnd[6] << 24) | 
			(scsicmd->cmnd[7] << 16) |
			(scsicmd->cmnd[8] << 8) | scsicmd->cmnd[9];
		count = (scsicmd->cmnd[10] << 24) | 
			(scsicmd->cmnd[11] << 16) |
			(scsicmd->cmnd[12] << 8) | scsicmd->cmnd[13];
		break;
	case READ_12:
		dprintk((KERN_DEBUG "aachba: received a read(12) command on id %d.\n", scmd_id(scsicmd)));

		lba = ((u64)scsicmd->cmnd[2] << 24) | 
			(scsicmd->cmnd[3] << 16) |
		    	(scsicmd->cmnd[4] << 8) | scsicmd->cmnd[5];
		count = (scsicmd->cmnd[6] << 24) | 
			(scsicmd->cmnd[7] << 16) |
		      	(scsicmd->cmnd[8] << 8) | scsicmd->cmnd[9];
		break;
	default:
		dprintk((KERN_DEBUG "aachba: received a read(10) command on id %d.\n", scmd_id(scsicmd)));

		lba = ((u64)scsicmd->cmnd[2] << 24) | 
			(scsicmd->cmnd[3] << 16) | 
			(scsicmd->cmnd[4] << 8) | scsicmd->cmnd[5];
		count = (scsicmd->cmnd[7] << 8) | scsicmd->cmnd[8];
		break;
	}
	dprintk((KERN_DEBUG "aac_read[cpu %d]: lba = %llu, t = %ld.\n",
	  smp_processor_id(), (unsigned long long)lba, jiffies));
	if (aac_adapter_bounds(dev,scsicmd,lba))
		return 0;
	/*
	 *	Alocate and initialize a Fib
	 */
	if (!(cmd_fibcontext = aac_fib_alloc(dev))) {
		return -1;
	}

	status = aac_adapter_read(cmd_fibcontext, scsicmd, lba, count);

	/*
	 *	Check that the command queued to the controller
	 */
	if (status == -EINPROGRESS) {
		scsicmd->SCp.phase = AAC_OWNER_FIRMWARE;
		return 0;
	}
		
	printk(KERN_WARNING "aac_read: aac_fib_send failed with status: %d.\n", status);
	/*
	 *	For some reason, the Fib didn't queue, return QUEUE_FULL
	 */
	scsicmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8 | SAM_STAT_TASK_SET_FULL;
	scsicmd->scsi_done(scsicmd);
	aac_fib_complete(cmd_fibcontext);
	aac_fib_free(cmd_fibcontext);
	return 0;
}

static int aac_write(struct scsi_cmnd * scsicmd)
{
	u64 lba;
	u32 count;
	int fua;
	int status;
	struct aac_dev *dev;
	struct fib * cmd_fibcontext;

	dev = (struct aac_dev *)scsicmd->device->host->hostdata;
	/*
	 *	Get block address and transfer length
	 */
	if (scsicmd->cmnd[0] == WRITE_6)	/* 6 byte command */
	{
		lba = ((scsicmd->cmnd[1] & 0x1F) << 16) | (scsicmd->cmnd[2] << 8) | scsicmd->cmnd[3];
		count = scsicmd->cmnd[4];
		if (count == 0)
			count = 256;
		fua = 0;
	} else if (scsicmd->cmnd[0] == WRITE_16) { /* 16 byte command */
		dprintk((KERN_DEBUG "aachba: received a write(16) command on id %d.\n", scmd_id(scsicmd)));

		lba = 	((u64)scsicmd->cmnd[2] << 56) |
			((u64)scsicmd->cmnd[3] << 48) |
			((u64)scsicmd->cmnd[4] << 40) |
			((u64)scsicmd->cmnd[5] << 32) |
			((u64)scsicmd->cmnd[6] << 24) | 
			(scsicmd->cmnd[7] << 16) |
			(scsicmd->cmnd[8] << 8) | scsicmd->cmnd[9];
		count = (scsicmd->cmnd[10] << 24) | (scsicmd->cmnd[11] << 16) |
			(scsicmd->cmnd[12] << 8) | scsicmd->cmnd[13];
		fua = scsicmd->cmnd[1] & 0x8;
	} else if (scsicmd->cmnd[0] == WRITE_12) { /* 12 byte command */
		dprintk((KERN_DEBUG "aachba: received a write(12) command on id %d.\n", scmd_id(scsicmd)));

		lba = ((u64)scsicmd->cmnd[2] << 24) | (scsicmd->cmnd[3] << 16)
		    | (scsicmd->cmnd[4] << 8) | scsicmd->cmnd[5];
		count = (scsicmd->cmnd[6] << 24) | (scsicmd->cmnd[7] << 16)
		      | (scsicmd->cmnd[8] << 8) | scsicmd->cmnd[9];
		fua = scsicmd->cmnd[1] & 0x8;
	} else {
		dprintk((KERN_DEBUG "aachba: received a write(10) command on id %d.\n", scmd_id(scsicmd)));
		lba = ((u64)scsicmd->cmnd[2] << 24) | (scsicmd->cmnd[3] << 16) | (scsicmd->cmnd[4] << 8) | scsicmd->cmnd[5];
		count = (scsicmd->cmnd[7] << 8) | scsicmd->cmnd[8];
		fua = scsicmd->cmnd[1] & 0x8;
	}
	dprintk((KERN_DEBUG "aac_write[cpu %d]: lba = %llu, t = %ld.\n",
	  smp_processor_id(), (unsigned long long)lba, jiffies));
	if (aac_adapter_bounds(dev,scsicmd,lba))
		return 0;
	/*
	 *	Allocate and initialize a Fib then setup a BlockWrite command
	 */
	if (!(cmd_fibcontext = aac_fib_alloc(dev))) {
		scsicmd->result = DID_ERROR << 16;
		scsicmd->scsi_done(scsicmd);
		return 0;
	}

	status = aac_adapter_write(cmd_fibcontext, scsicmd, lba, count, fua);

	/*
	 *	Check that the command queued to the controller
	 */
	if (status == -EINPROGRESS) {
		scsicmd->SCp.phase = AAC_OWNER_FIRMWARE;
		return 0;
	}

	printk(KERN_WARNING "aac_write: aac_fib_send failed with status: %d\n", status);
	/*
	 *	For some reason, the Fib didn't queue, return QUEUE_FULL
	 */
	scsicmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8 | SAM_STAT_TASK_SET_FULL;
	scsicmd->scsi_done(scsicmd);

	aac_fib_complete(cmd_fibcontext);
	aac_fib_free(cmd_fibcontext);
	return 0;
}

static void synchronize_callback(void *context, struct fib *fibptr)
{
	struct aac_synchronize_reply *synchronizereply;
	struct scsi_cmnd *cmd;

	cmd = context;

	if (!aac_valid_context(cmd, fibptr))
		return;

	dprintk((KERN_DEBUG "synchronize_callback[cpu %d]: t = %ld.\n", 
				smp_processor_id(), jiffies));
	BUG_ON(fibptr == NULL);


	synchronizereply = fib_data(fibptr);
	if (le32_to_cpu(synchronizereply->status) == CT_OK)
		cmd->result = DID_OK << 16 | 
			COMMAND_COMPLETE << 8 | SAM_STAT_GOOD;
	else {
		struct scsi_device *sdev = cmd->device;
		struct aac_dev *dev = fibptr->dev;
		u32 cid = sdev_id(sdev);
		printk(KERN_WARNING 
		     "synchronize_callback: synchronize failed, status = %d\n",
		     le32_to_cpu(synchronizereply->status));
		cmd->result = DID_OK << 16 | 
			COMMAND_COMPLETE << 8 | SAM_STAT_CHECK_CONDITION;
		set_sense((u8 *)&dev->fsa_dev[cid].sense_data,
				    HARDWARE_ERROR,
				    SENCODE_INTERNAL_TARGET_FAILURE,
				    ASENCODE_INTERNAL_TARGET_FAILURE, 0, 0,
				    0, 0);
		memcpy(cmd->sense_buffer, &dev->fsa_dev[cid].sense_data,
		  min(sizeof(dev->fsa_dev[cid].sense_data), 
			  sizeof(cmd->sense_buffer)));
	}

	aac_fib_complete(fibptr);
	aac_fib_free(fibptr);
	cmd->scsi_done(cmd);
}

static int aac_synchronize(struct scsi_cmnd *scsicmd)
{
	int status;
	struct fib *cmd_fibcontext;
	struct aac_synchronize *synchronizecmd;
	struct scsi_cmnd *cmd;
	struct scsi_device *sdev = scsicmd->device;
	int active = 0;
	struct aac_dev *aac;
	unsigned long flags;

	/*
	 * Wait for all outstanding queued commands to complete to this
	 * specific target (block).
	 */
	spin_lock_irqsave(&sdev->list_lock, flags);
	list_for_each_entry(cmd, &sdev->cmd_list, list)
		if (cmd != scsicmd && cmd->SCp.phase == AAC_OWNER_FIRMWARE) {
			++active;
			break;
		}

	spin_unlock_irqrestore(&sdev->list_lock, flags);

	/*
	 *	Yield the processor (requeue for later)
	 */
	if (active)
		return SCSI_MLQUEUE_DEVICE_BUSY;

	aac = (struct aac_dev *)scsicmd->device->host->hostdata;
	if (aac->in_reset)
		return SCSI_MLQUEUE_HOST_BUSY;

	/*
	 *	Allocate and initialize a Fib
	 */
	if (!(cmd_fibcontext = aac_fib_alloc(aac)))
		return SCSI_MLQUEUE_HOST_BUSY;

	aac_fib_init(cmd_fibcontext);

	synchronizecmd = fib_data(cmd_fibcontext);
	synchronizecmd->command = cpu_to_le32(VM_ContainerConfig);
	synchronizecmd->type = cpu_to_le32(CT_FLUSH_CACHE);
	synchronizecmd->cid = cpu_to_le32(scmd_id(scsicmd));
	synchronizecmd->count = 
	     cpu_to_le32(sizeof(((struct aac_synchronize_reply *)NULL)->data));

	/*
	 *	Now send the Fib to the adapter
	 */
	status = aac_fib_send(ContainerCommand,
		  cmd_fibcontext,
		  sizeof(struct aac_synchronize),
		  FsaNormal,
		  0, 1,
		  (fib_callback)synchronize_callback,
		  (void *)scsicmd);

	/*
	 *	Check that the command queued to the controller
	 */
	if (status == -EINPROGRESS) {
		scsicmd->SCp.phase = AAC_OWNER_FIRMWARE;
		return 0;
	}

	printk(KERN_WARNING 
		"aac_synchronize: aac_fib_send failed with status: %d.\n", status);
	aac_fib_complete(cmd_fibcontext);
	aac_fib_free(cmd_fibcontext);
	return SCSI_MLQUEUE_HOST_BUSY;
}

/**
 *	aac_scsi_cmd()		-	Process SCSI command
 *	@scsicmd:		SCSI command block
 *
 *	Emulate a SCSI command and queue the required request for the
 *	aacraid firmware.
 */
 
int aac_scsi_cmd(struct scsi_cmnd * scsicmd)
{
	u32 cid;
	struct Scsi_Host *host = scsicmd->device->host;
	struct aac_dev *dev = (struct aac_dev *)host->hostdata;
	struct fsa_dev_info *fsa_dev_ptr = dev->fsa_dev;
	
	if (fsa_dev_ptr == NULL)
		return -1;
	/*
	 *	If the bus, id or lun is out of range, return fail
	 *	Test does not apply to ID 16, the pseudo id for the controller
	 *	itself.
	 */
	cid = scmd_id(scsicmd);
	if (cid != host->this_id) {
		if (scmd_channel(scsicmd) == CONTAINER_CHANNEL) {
			if((cid >= dev->maximum_num_containers) ||
					(scsicmd->device->lun != 0)) {
				scsicmd->result = DID_NO_CONNECT << 16;
				scsicmd->scsi_done(scsicmd);
				return 0;
			}

			/*
			 *	If the target container doesn't exist, it may have
			 *	been newly created
			 */
			if ((fsa_dev_ptr[cid].valid & 1) == 0) {
				switch (scsicmd->cmnd[0]) {
				case SERVICE_ACTION_IN:
					if (!(dev->raw_io_interface) ||
					    !(dev->raw_io_64) ||
					    ((scsicmd->cmnd[1] & 0x1f) != SAI_READ_CAPACITY_16))
						break;
				case INQUIRY:
				case READ_CAPACITY:
				case TEST_UNIT_READY:
					if (dev->in_reset)
						return -1;
					return _aac_probe_container(scsicmd,
							aac_probe_container_callback2);
				default:
					break;
				}
			}
		} else {  /* check for physical non-dasd devices */
			if ((dev->nondasd_support == 1) || expose_physicals) {
				if (dev->in_reset)
					return -1;
				return aac_send_srb_fib(scsicmd);
			} else {
				scsicmd->result = DID_NO_CONNECT << 16;
				scsicmd->scsi_done(scsicmd);
				return 0;
			}
		}
	}
	/*
	 * else Command for the controller itself
	 */
	else if ((scsicmd->cmnd[0] != INQUIRY) &&	/* only INQUIRY & TUR cmnd supported for controller */
		(scsicmd->cmnd[0] != TEST_UNIT_READY)) 
	{
		dprintk((KERN_WARNING "Only INQUIRY & TUR command supported for controller, rcvd = 0x%x.\n", scsicmd->cmnd[0]));
		scsicmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8 | SAM_STAT_CHECK_CONDITION;
		set_sense((u8 *) &dev->fsa_dev[cid].sense_data,
			    ILLEGAL_REQUEST,
			    SENCODE_INVALID_COMMAND,
			    ASENCODE_INVALID_COMMAND, 0, 0, 0, 0);
		memcpy(scsicmd->sense_buffer, &dev->fsa_dev[cid].sense_data,
		  (sizeof(dev->fsa_dev[cid].sense_data) > sizeof(scsicmd->sense_buffer))
		    ? sizeof(scsicmd->sense_buffer)
		    : sizeof(dev->fsa_dev[cid].sense_data));
		scsicmd->scsi_done(scsicmd);
		return 0;
	}


	/* Handle commands here that don't really require going out to the adapter */
	switch (scsicmd->cmnd[0]) {
	case INQUIRY:
	{
		struct inquiry_data inq_data;

		dprintk((KERN_DEBUG "INQUIRY command, ID: %d.\n", cid));
		memset(&inq_data, 0, sizeof (struct inquiry_data));

		if (scsicmd->cmnd[1] & 0x1 ) {
			char *arr = (char *)&inq_data;

			/* EVPD bit set */
			arr[0] = (scmd_id(scsicmd) == host->this_id) ?
			  INQD_PDT_PROC : INQD_PDT_DA;
			if (scsicmd->cmnd[2] == 0) {
				/* supported vital product data pages */
				arr[3] = 2;
				arr[4] = 0x0;
				arr[5] = 0x80;
				arr[1] = scsicmd->cmnd[2];
				aac_internal_transfer(scsicmd, &inq_data, 0,
				  sizeof(inq_data));
				scsicmd->result = DID_OK << 16 |
				  COMMAND_COMPLETE << 8 | SAM_STAT_GOOD;
			} else if (scsicmd->cmnd[2] == 0x80) {
				/* unit serial number page */
				arr[3] = setinqserial(dev, &arr[4],
				  scmd_id(scsicmd));
				arr[1] = scsicmd->cmnd[2];
				aac_internal_transfer(scsicmd, &inq_data, 0,
				  sizeof(inq_data));
				return aac_get_container_serial(scsicmd);
			} else {
				/* vpd page not implemented */
				scsicmd->result = DID_OK << 16 |
				  COMMAND_COMPLETE << 8 |
				  SAM_STAT_CHECK_CONDITION;
				set_sense((u8 *) &dev->fsa_dev[cid].sense_data,
				  ILLEGAL_REQUEST,
				  SENCODE_INVALID_CDB_FIELD,
				  ASENCODE_NO_SENSE, 0, 7, 2, 0);
				memcpy(scsicmd->sense_buffer,
				  &dev->fsa_dev[cid].sense_data,
				  (sizeof(dev->fsa_dev[cid].sense_data) >
				    sizeof(scsicmd->sense_buffer))
				       ? sizeof(scsicmd->sense_buffer)
				       : sizeof(dev->fsa_dev[cid].sense_data));
			}
			scsicmd->scsi_done(scsicmd);
			return 0;
		}
		inq_data.inqd_ver = 2;	/* claim compliance to SCSI-2 */
		inq_data.inqd_rdf = 2;	/* A response data format value of two indicates that the data shall be in the format specified in SCSI-2 */
		inq_data.inqd_len = 31;
		/*Format for "pad2" is  RelAdr | WBus32 | WBus16 |  Sync  | Linked |Reserved| CmdQue | SftRe */
		inq_data.inqd_pad2= 0x32 ;	 /*WBus16|Sync|CmdQue */
		/*
		 *	Set the Vendor, Product, and Revision Level
		 *	see: <vendor>.c i.e. aac.c
		 */
		if (cid == host->this_id) {
			setinqstr(dev, (void *) (inq_data.inqd_vid), ARRAY_SIZE(container_types));
			inq_data.inqd_pdt = INQD_PDT_PROC;	/* Processor device */
			aac_internal_transfer(scsicmd, &inq_data, 0, sizeof(inq_data));
			scsicmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8 | SAM_STAT_GOOD;
			scsicmd->scsi_done(scsicmd);
			return 0;
		}
		if (dev->in_reset)
			return -1;
		setinqstr(dev, (void *) (inq_data.inqd_vid), fsa_dev_ptr[cid].type);
		inq_data.inqd_pdt = INQD_PDT_DA;	/* Direct/random access device */
		aac_internal_transfer(scsicmd, &inq_data, 0, sizeof(inq_data));
		return aac_get_container_name(scsicmd);
	}
	case SERVICE_ACTION_IN:
		if (!(dev->raw_io_interface) ||
		    !(dev->raw_io_64) ||
		    ((scsicmd->cmnd[1] & 0x1f) != SAI_READ_CAPACITY_16))
			break;
	{
		u64 capacity;
		char cp[13];

		dprintk((KERN_DEBUG "READ CAPACITY_16 command.\n"));
		capacity = fsa_dev_ptr[cid].size - 1;
		cp[0] = (capacity >> 56) & 0xff;
		cp[1] = (capacity >> 48) & 0xff;
		cp[2] = (capacity >> 40) & 0xff;
		cp[3] = (capacity >> 32) & 0xff;
		cp[4] = (capacity >> 24) & 0xff;
		cp[5] = (capacity >> 16) & 0xff;
		cp[6] = (capacity >> 8) & 0xff;
		cp[7] = (capacity >> 0) & 0xff;
		cp[8] = 0;
		cp[9] = 0;
		cp[10] = 2;
		cp[11] = 0;
		cp[12] = 0;
		aac_internal_transfer(scsicmd, cp, 0,
		  min_t(size_t, scsicmd->cmnd[13], sizeof(cp)));
		if (sizeof(cp) < scsicmd->cmnd[13]) {
			unsigned int len, offset = sizeof(cp);

			memset(cp, 0, offset);
			do {
				len = min_t(size_t, scsicmd->cmnd[13] - offset,
						sizeof(cp));
				aac_internal_transfer(scsicmd, cp, offset, len);
			} while ((offset += len) < scsicmd->cmnd[13]);
		}

		/* Do not cache partition table for arrays */
		scsicmd->device->removable = 1;

		scsicmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8 | SAM_STAT_GOOD;
		scsicmd->scsi_done(scsicmd);

		return 0;
	}

	case READ_CAPACITY:
	{
		u32 capacity;
		char cp[8];

		dprintk((KERN_DEBUG "READ CAPACITY command.\n"));
		if (fsa_dev_ptr[cid].size <= 0x100000000ULL)
			capacity = fsa_dev_ptr[cid].size - 1;
		else
			capacity = (u32)-1;

		cp[0] = (capacity >> 24) & 0xff;
		cp[1] = (capacity >> 16) & 0xff;
		cp[2] = (capacity >> 8) & 0xff;
		cp[3] = (capacity >> 0) & 0xff;
		cp[4] = 0;
		cp[5] = 0;
		cp[6] = 2;
		cp[7] = 0;
		aac_internal_transfer(scsicmd, cp, 0, sizeof(cp));
		/* Do not cache partition table for arrays */
		scsicmd->device->removable = 1;

		scsicmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8 | SAM_STAT_GOOD;
		scsicmd->scsi_done(scsicmd);

		return 0;
	}

	case MODE_SENSE:
	{
		char mode_buf[7];
		int mode_buf_length = 4;

		dprintk((KERN_DEBUG "MODE SENSE command.\n"));
		mode_buf[0] = 3;	/* Mode data length */
		mode_buf[1] = 0;	/* Medium type - default */
		mode_buf[2] = 0;	/* Device-specific param,
					   bit 8: 0/1 = write enabled/protected
					   bit 4: 0/1 = FUA enabled */
		if (dev->raw_io_interface)
			mode_buf[2] = 0x10;
		mode_buf[3] = 0;	/* Block descriptor length */
		if (((scsicmd->cmnd[2] & 0x3f) == 8) ||
		  ((scsicmd->cmnd[2] & 0x3f) == 0x3f)) {
			mode_buf[0] = 6;
			mode_buf[4] = 8;
			mode_buf[5] = 1;
			mode_buf[6] = 0x04; /* WCE */
			mode_buf_length = 7;
			if (mode_buf_length > scsicmd->cmnd[4])
				mode_buf_length = scsicmd->cmnd[4];
		}
		aac_internal_transfer(scsicmd, mode_buf, 0, mode_buf_length);
		scsicmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8 | SAM_STAT_GOOD;
		scsicmd->scsi_done(scsicmd);

		return 0;
	}
	case MODE_SENSE_10:
	{
		char mode_buf[11];
		int mode_buf_length = 8;

		dprintk((KERN_DEBUG "MODE SENSE 10 byte command.\n"));
		mode_buf[0] = 0;	/* Mode data length (MSB) */
		mode_buf[1] = 6;	/* Mode data length (LSB) */
		mode_buf[2] = 0;	/* Medium type - default */
		mode_buf[3] = 0;	/* Device-specific param,
					   bit 8: 0/1 = write enabled/protected
					   bit 4: 0/1 = FUA enabled */
		if (dev->raw_io_interface)
			mode_buf[3] = 0x10;
		mode_buf[4] = 0;	/* reserved */
		mode_buf[5] = 0;	/* reserved */
		mode_buf[6] = 0;	/* Block descriptor length (MSB) */
		mode_buf[7] = 0;	/* Block descriptor length (LSB) */
		if (((scsicmd->cmnd[2] & 0x3f) == 8) ||
		  ((scsicmd->cmnd[2] & 0x3f) == 0x3f)) {
			mode_buf[1] = 9;
			mode_buf[8] = 8;
			mode_buf[9] = 1;
			mode_buf[10] = 0x04; /* WCE */
			mode_buf_length = 11;
			if (mode_buf_length > scsicmd->cmnd[8])
				mode_buf_length = scsicmd->cmnd[8];
		}
		aac_internal_transfer(scsicmd, mode_buf, 0, mode_buf_length);

		scsicmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8 | SAM_STAT_GOOD;
		scsicmd->scsi_done(scsicmd);

		return 0;
	}
	case REQUEST_SENSE:
		dprintk((KERN_DEBUG "REQUEST SENSE command.\n"));
		memcpy(scsicmd->sense_buffer, &dev->fsa_dev[cid].sense_data, sizeof (struct sense_data));
		memset(&dev->fsa_dev[cid].sense_data, 0, sizeof (struct sense_data));
		scsicmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8 | SAM_STAT_GOOD;
		scsicmd->scsi_done(scsicmd);
		return 0;

	case ALLOW_MEDIUM_REMOVAL:
		dprintk((KERN_DEBUG "LOCK command.\n"));
		if (scsicmd->cmnd[4])
			fsa_dev_ptr[cid].locked = 1;
		else
			fsa_dev_ptr[cid].locked = 0;

		scsicmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8 | SAM_STAT_GOOD;
		scsicmd->scsi_done(scsicmd);
		return 0;
	/*
	 *	These commands are all No-Ops
	 */
	case TEST_UNIT_READY:
	case RESERVE:
	case RELEASE:
	case REZERO_UNIT:
	case REASSIGN_BLOCKS:
	case SEEK_10:
	case START_STOP:
		scsicmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8 | SAM_STAT_GOOD;
		scsicmd->scsi_done(scsicmd);
		return 0;
	}

	switch (scsicmd->cmnd[0]) 
	{
		case READ_6:
		case READ_10:
		case READ_12:
		case READ_16:
			if (dev->in_reset)
				return -1;
			/*
			 *	Hack to keep track of ordinal number of the device that
			 *	corresponds to a container. Needed to convert
			 *	containers to /dev/sd device names
			 */
			 
			if (scsicmd->request->rq_disk)
				strlcpy(fsa_dev_ptr[cid].devname,
				scsicmd->request->rq_disk->disk_name,
			  	min(sizeof(fsa_dev_ptr[cid].devname),
				sizeof(scsicmd->request->rq_disk->disk_name) + 1));

			return aac_read(scsicmd);

		case WRITE_6:
		case WRITE_10:
		case WRITE_12:
		case WRITE_16:
			if (dev->in_reset)
				return -1;
			return aac_write(scsicmd);

		case SYNCHRONIZE_CACHE:
			/* Issue FIB to tell Firmware to flush it's cache */
			return aac_synchronize(scsicmd);
			
		default:
			/*
			 *	Unhandled commands
			 */
			dprintk((KERN_WARNING "Unhandled SCSI Command: 0x%x.\n", scsicmd->cmnd[0]));
			scsicmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8 | SAM_STAT_CHECK_CONDITION;
			set_sense((u8 *) &dev->fsa_dev[cid].sense_data,
				ILLEGAL_REQUEST, SENCODE_INVALID_COMMAND,
				ASENCODE_INVALID_COMMAND, 0, 0, 0, 0);
			memcpy(scsicmd->sense_buffer, &dev->fsa_dev[cid].sense_data,
			  (sizeof(dev->fsa_dev[cid].sense_data) > sizeof(scsicmd->sense_buffer))
			    ? sizeof(scsicmd->sense_buffer)
			    : sizeof(dev->fsa_dev[cid].sense_data));
			scsicmd->scsi_done(scsicmd);
			return 0;
	}
}

static int query_disk(struct aac_dev *dev, void __user *arg)
{
	struct aac_query_disk qd;
	struct fsa_dev_info *fsa_dev_ptr;

	fsa_dev_ptr = dev->fsa_dev;
	if (!fsa_dev_ptr)
		return -EBUSY;
	if (copy_from_user(&qd, arg, sizeof (struct aac_query_disk)))
		return -EFAULT;
	if (qd.cnum == -1)
		qd.cnum = qd.id;
	else if ((qd.bus == -1) && (qd.id == -1) && (qd.lun == -1)) 
	{
		if (qd.cnum < 0 || qd.cnum >= dev->maximum_num_containers)
			return -EINVAL;
		qd.instance = dev->scsi_host_ptr->host_no;
		qd.bus = 0;
		qd.id = CONTAINER_TO_ID(qd.cnum);
		qd.lun = CONTAINER_TO_LUN(qd.cnum);
	}
	else return -EINVAL;

	qd.valid = fsa_dev_ptr[qd.cnum].valid != 0;
	qd.locked = fsa_dev_ptr[qd.cnum].locked;
	qd.deleted = fsa_dev_ptr[qd.cnum].deleted;

	if (fsa_dev_ptr[qd.cnum].devname[0] == '\0')
		qd.unmapped = 1;
	else
		qd.unmapped = 0;

	strlcpy(qd.name, fsa_dev_ptr[qd.cnum].devname,
	  min(sizeof(qd.name), sizeof(fsa_dev_ptr[qd.cnum].devname) + 1));

	if (copy_to_user(arg, &qd, sizeof (struct aac_query_disk)))
		return -EFAULT;
	return 0;
}

static int force_delete_disk(struct aac_dev *dev, void __user *arg)
{
	struct aac_delete_disk dd;
	struct fsa_dev_info *fsa_dev_ptr;

	fsa_dev_ptr = dev->fsa_dev;
	if (!fsa_dev_ptr)
		return -EBUSY;

	if (copy_from_user(&dd, arg, sizeof (struct aac_delete_disk)))
		return -EFAULT;

	if (dd.cnum >= dev->maximum_num_containers)
		return -EINVAL;
	/*
	 *	Mark this container as being deleted.
	 */
	fsa_dev_ptr[dd.cnum].deleted = 1;
	/*
	 *	Mark the container as no longer valid
	 */
	fsa_dev_ptr[dd.cnum].valid = 0;
	return 0;
}

static int delete_disk(struct aac_dev *dev, void __user *arg)
{
	struct aac_delete_disk dd;
	struct fsa_dev_info *fsa_dev_ptr;

	fsa_dev_ptr = dev->fsa_dev;
	if (!fsa_dev_ptr)
		return -EBUSY;

	if (copy_from_user(&dd, arg, sizeof (struct aac_delete_disk)))
		return -EFAULT;

	if (dd.cnum >= dev->maximum_num_containers)
		return -EINVAL;
	/*
	 *	If the container is locked, it can not be deleted by the API.
	 */
	if (fsa_dev_ptr[dd.cnum].locked)
		return -EBUSY;
	else {
		/*
		 *	Mark the container as no longer being valid.
		 */
		fsa_dev_ptr[dd.cnum].valid = 0;
		fsa_dev_ptr[dd.cnum].devname[0] = '\0';
		return 0;
	}
}

int aac_dev_ioctl(struct aac_dev *dev, int cmd, void __user *arg)
{
	switch (cmd) {
	case FSACTL_QUERY_DISK:
		return query_disk(dev, arg);
	case FSACTL_DELETE_DISK:
		return delete_disk(dev, arg);
	case FSACTL_FORCE_DELETE_DISK:
		return force_delete_disk(dev, arg);
	case FSACTL_GET_CONTAINERS:
		return aac_get_containers(dev);
	default:
		return -ENOTTY;
	}
}

/**
 *
 * aac_srb_callback
 * @context: the context set in the fib - here it is scsi cmd
 * @fibptr: pointer to the fib
 *
 * Handles the completion of a scsi command to a non dasd device
 *
 */

static void aac_srb_callback(void *context, struct fib * fibptr)
{
	struct aac_dev *dev;
	struct aac_srb_reply *srbreply;
	struct scsi_cmnd *scsicmd;

	scsicmd = (struct scsi_cmnd *) context;

	if (!aac_valid_context(scsicmd, fibptr))
		return;

	BUG_ON(fibptr == NULL);

	dev = fibptr->dev;

	srbreply = (struct aac_srb_reply *) fib_data(fibptr);

	scsicmd->sense_buffer[0] = '\0';  /* Initialize sense valid flag to false */
	/*
	 *	Calculate resid for sg 
	 */

	scsi_set_resid(scsicmd, scsi_bufflen(scsicmd)
		       - le32_to_cpu(srbreply->data_xfer_length));

	scsi_dma_unmap(scsicmd);

	/*
	 * First check the fib status
	 */

	if (le32_to_cpu(srbreply->status) != ST_OK){
		int len;
		printk(KERN_WARNING "aac_srb_callback: srb failed, status = %d\n", le32_to_cpu(srbreply->status));
		len = (le32_to_cpu(srbreply->sense_data_size) > 
				sizeof(scsicmd->sense_buffer)) ?
				sizeof(scsicmd->sense_buffer) : 
				le32_to_cpu(srbreply->sense_data_size);
		scsicmd->result = DID_ERROR << 16 | COMMAND_COMPLETE << 8 | SAM_STAT_CHECK_CONDITION;
		memcpy(scsicmd->sense_buffer, srbreply->sense_data, len);
	}

	/*
	 * Next check the srb status
	 */
	switch( (le32_to_cpu(srbreply->srb_status))&0x3f){
	case SRB_STATUS_ERROR_RECOVERY:
	case SRB_STATUS_PENDING:
	case SRB_STATUS_SUCCESS:
		scsicmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8;
		break;
	case SRB_STATUS_DATA_OVERRUN:
		switch(scsicmd->cmnd[0]){
		case  READ_6:
		case  WRITE_6:
		case  READ_10:
		case  WRITE_10:
		case  READ_12:
		case  WRITE_12:
		case  READ_16:
		case  WRITE_16:
			if(le32_to_cpu(srbreply->data_xfer_length) < scsicmd->underflow ) {
				printk(KERN_WARNING"aacraid: SCSI CMD underflow\n");
			} else {
				printk(KERN_WARNING"aacraid: SCSI CMD Data Overrun\n");
			}
			scsicmd->result = DID_ERROR << 16 | COMMAND_COMPLETE << 8;
			break;
		case INQUIRY: {
			scsicmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8;
			break;
		}
		default:
			scsicmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8;
			break;
		}
		break;
	case SRB_STATUS_ABORTED:
		scsicmd->result = DID_ABORT << 16 | ABORT << 8;
		break;
	case SRB_STATUS_ABORT_FAILED:
		// Not sure about this one - but assuming the hba was trying to abort for some reason
		scsicmd->result = DID_ERROR << 16 | ABORT << 8;
		break;
	case SRB_STATUS_PARITY_ERROR:
		scsicmd->result = DID_PARITY << 16 | MSG_PARITY_ERROR << 8;
		break;
	case SRB_STATUS_NO_DEVICE:
	case SRB_STATUS_INVALID_PATH_ID:
	case SRB_STATUS_INVALID_TARGET_ID:
	case SRB_STATUS_INVALID_LUN:
	case SRB_STATUS_SELECTION_TIMEOUT:
		scsicmd->result = DID_NO_CONNECT << 16 | COMMAND_COMPLETE << 8;
		break;

	case SRB_STATUS_COMMAND_TIMEOUT:
	case SRB_STATUS_TIMEOUT:
		scsicmd->result = DID_TIME_OUT << 16 | COMMAND_COMPLETE << 8;
		break;

	case SRB_STATUS_BUSY:
		scsicmd->result = DID_BUS_BUSY << 16 | COMMAND_COMPLETE << 8;
		break;

	case SRB_STATUS_BUS_RESET:
		scsicmd->result = DID_RESET << 16 | COMMAND_COMPLETE << 8;
		break;

	case SRB_STATUS_MESSAGE_REJECTED:
		scsicmd->result = DID_ERROR << 16 | MESSAGE_REJECT << 8;
		break;
	case SRB_STATUS_REQUEST_FLUSHED:
	case SRB_STATUS_ERROR:
	case SRB_STATUS_INVALID_REQUEST:
	case SRB_STATUS_REQUEST_SENSE_FAILED:
	case SRB_STATUS_NO_HBA:
	case SRB_STATUS_UNEXPECTED_BUS_FREE:
	case SRB_STATUS_PHASE_SEQUENCE_FAILURE:
	case SRB_STATUS_BAD_SRB_BLOCK_LENGTH:
	case SRB_STATUS_DELAYED_RETRY:
	case SRB_STATUS_BAD_FUNCTION:
	case SRB_STATUS_NOT_STARTED:
	case SRB_STATUS_NOT_IN_USE:
	case SRB_STATUS_FORCE_ABORT:
	case SRB_STATUS_DOMAIN_VALIDATION_FAIL:
	default:
#ifdef AAC_DETAILED_STATUS_INFO
		printk("aacraid: SRB ERROR(%u) %s scsi cmd 0x%x - scsi status 0x%x\n",
			le32_to_cpu(srbreply->srb_status) & 0x3F,
			aac_get_status_string(
				le32_to_cpu(srbreply->srb_status) & 0x3F), 
			scsicmd->cmnd[0], 
			le32_to_cpu(srbreply->scsi_status));
#endif
		scsicmd->result = DID_ERROR << 16 | COMMAND_COMPLETE << 8;
		break;
	}
	if (le32_to_cpu(srbreply->scsi_status) == 0x02 ){  // Check Condition
		int len;
		scsicmd->result |= SAM_STAT_CHECK_CONDITION;
		len = (le32_to_cpu(srbreply->sense_data_size) > 
				sizeof(scsicmd->sense_buffer)) ?
				sizeof(scsicmd->sense_buffer) :
				le32_to_cpu(srbreply->sense_data_size);
#ifdef AAC_DETAILED_STATUS_INFO
		printk(KERN_WARNING "aac_srb_callback: check condition, status = %d len=%d\n",
					le32_to_cpu(srbreply->status), len);
#endif
		memcpy(scsicmd->sense_buffer, srbreply->sense_data, len);
		
	}
	/*
	 * OR in the scsi status (already shifted up a bit)
	 */
	scsicmd->result |= le32_to_cpu(srbreply->scsi_status);

	aac_fib_complete(fibptr);
	aac_fib_free(fibptr);
	scsicmd->scsi_done(scsicmd);
}

/**
 *
 * aac_send_scb_fib
 * @scsicmd: the scsi command block
 *
 * This routine will form a FIB and fill in the aac_srb from the 
 * scsicmd passed in.
 */

static int aac_send_srb_fib(struct scsi_cmnd* scsicmd)
{
	struct fib* cmd_fibcontext;
	struct aac_dev* dev;
	int status;

	dev = (struct aac_dev *)scsicmd->device->host->hostdata;
	if (scmd_id(scsicmd) >= dev->maximum_num_physicals ||
			scsicmd->device->lun > 7) {
		scsicmd->result = DID_NO_CONNECT << 16;
		scsicmd->scsi_done(scsicmd);
		return 0;
	}

	/*
	 *	Allocate and initialize a Fib then setup a BlockWrite command
	 */
	if (!(cmd_fibcontext = aac_fib_alloc(dev))) {
		return -1;
	}
	status = aac_adapter_scsi(cmd_fibcontext, scsicmd);

	/*
	 *	Check that the command queued to the controller
	 */
	if (status == -EINPROGRESS) {
		scsicmd->SCp.phase = AAC_OWNER_FIRMWARE;
		return 0;
	}

	printk(KERN_WARNING "aac_srb: aac_fib_send failed with status: %d\n", status);
	aac_fib_complete(cmd_fibcontext);
	aac_fib_free(cmd_fibcontext);

	return -1;
}

static unsigned long aac_build_sg(struct scsi_cmnd* scsicmd, struct sgmap* psg)
{
	struct aac_dev *dev;
	unsigned long byte_count = 0;
	int nseg;

	dev = (struct aac_dev *)scsicmd->device->host->hostdata;
	// Get rid of old data
	psg->count = 0;
	psg->sg[0].addr = 0;
	psg->sg[0].count = 0;

	nseg = scsi_dma_map(scsicmd);
	BUG_ON(nseg < 0);
	if (nseg) {
		struct scatterlist *sg;
		int i;

		psg->count = cpu_to_le32(nseg);

		scsi_for_each_sg(scsicmd, sg, nseg, i) {
			psg->sg[i].addr = cpu_to_le32(sg_dma_address(sg));
			psg->sg[i].count = cpu_to_le32(sg_dma_len(sg));
			byte_count += sg_dma_len(sg);
		}
		/* hba wants the size to be exact */
		if (byte_count > scsi_bufflen(scsicmd)) {
			u32 temp = le32_to_cpu(psg->sg[i-1].count) -
				(byte_count - scsi_bufflen(scsicmd));
			psg->sg[i-1].count = cpu_to_le32(temp);
			byte_count = scsi_bufflen(scsicmd);
		}
		/* Check for command underflow */
		if(scsicmd->underflow && (byte_count < scsicmd->underflow)){
			printk(KERN_WARNING"aacraid: cmd len %08lX cmd underflow %08X\n",
					byte_count, scsicmd->underflow);
		}
	}
	return byte_count;
}


static unsigned long aac_build_sg64(struct scsi_cmnd* scsicmd, struct sgmap64* psg)
{
	struct aac_dev *dev;
	unsigned long byte_count = 0;
	u64 addr;
	int nseg;

	dev = (struct aac_dev *)scsicmd->device->host->hostdata;
	// Get rid of old data
	psg->count = 0;
	psg->sg[0].addr[0] = 0;
	psg->sg[0].addr[1] = 0;
	psg->sg[0].count = 0;

	nseg = scsi_dma_map(scsicmd);
	BUG_ON(nseg < 0);
	if (nseg) {
		struct scatterlist *sg;
		int i;

		scsi_for_each_sg(scsicmd, sg, nseg, i) {
			int count = sg_dma_len(sg);
			addr = sg_dma_address(sg);
			psg->sg[i].addr[0] = cpu_to_le32(addr & 0xffffffff);
			psg->sg[i].addr[1] = cpu_to_le32(addr>>32);
			psg->sg[i].count = cpu_to_le32(count);
			byte_count += count;
		}
		psg->count = cpu_to_le32(nseg);
		/* hba wants the size to be exact */
		if (byte_count > scsi_bufflen(scsicmd)) {
			u32 temp = le32_to_cpu(psg->sg[i-1].count) -
				(byte_count - scsi_bufflen(scsicmd));
			psg->sg[i-1].count = cpu_to_le32(temp);
			byte_count = scsi_bufflen(scsicmd);
		}
		/* Check for command underflow */
		if(scsicmd->underflow && (byte_count < scsicmd->underflow)){
			printk(KERN_WARNING"aacraid: cmd len %08lX cmd underflow %08X\n",
					byte_count, scsicmd->underflow);
		}
	}
	return byte_count;
}

static unsigned long aac_build_sgraw(struct scsi_cmnd* scsicmd, struct sgmapraw* psg)
{
	unsigned long byte_count = 0;
	int nseg;

	// Get rid of old data
	psg->count = 0;
	psg->sg[0].next = 0;
	psg->sg[0].prev = 0;
	psg->sg[0].addr[0] = 0;
	psg->sg[0].addr[1] = 0;
	psg->sg[0].count = 0;
	psg->sg[0].flags = 0;

	nseg = scsi_dma_map(scsicmd);
	BUG_ON(nseg < 0);
	if (nseg) {
		struct scatterlist *sg;
		int i;

		scsi_for_each_sg(scsicmd, sg, nseg, i) {
			int count = sg_dma_len(sg);
			u64 addr = sg_dma_address(sg);
			psg->sg[i].next = 0;
			psg->sg[i].prev = 0;
			psg->sg[i].addr[1] = cpu_to_le32((u32)(addr>>32));
			psg->sg[i].addr[0] = cpu_to_le32((u32)(addr & 0xffffffff));
			psg->sg[i].count = cpu_to_le32(count);
			psg->sg[i].flags = 0;
			byte_count += count;
		}
		psg->count = cpu_to_le32(nseg);
		/* hba wants the size to be exact */
		if (byte_count > scsi_bufflen(scsicmd)) {
			u32 temp = le32_to_cpu(psg->sg[i-1].count) -
				(byte_count - scsi_bufflen(scsicmd));
			psg->sg[i-1].count = cpu_to_le32(temp);
			byte_count = scsi_bufflen(scsicmd);
		}
		/* Check for command underflow */
		if(scsicmd->underflow && (byte_count < scsicmd->underflow)){
			printk(KERN_WARNING"aacraid: cmd len %08lX cmd underflow %08X\n",
					byte_count, scsicmd->underflow);
		}
	}
	return byte_count;
}

#ifdef AAC_DETAILED_STATUS_INFO

struct aac_srb_status_info {
	u32	status;
	char	*str;
};


static struct aac_srb_status_info srb_status_info[] = {
	{ SRB_STATUS_PENDING,		"Pending Status"},
	{ SRB_STATUS_SUCCESS,		"Success"},
	{ SRB_STATUS_ABORTED,		"Aborted Command"},
	{ SRB_STATUS_ABORT_FAILED,	"Abort Failed"},
	{ SRB_STATUS_ERROR,		"Error Event"},
	{ SRB_STATUS_BUSY,		"Device Busy"},
	{ SRB_STATUS_INVALID_REQUEST,	"Invalid Request"},
	{ SRB_STATUS_INVALID_PATH_ID,	"Invalid Path ID"},
	{ SRB_STATUS_NO_DEVICE,		"No Device"},
	{ SRB_STATUS_TIMEOUT,		"Timeout"},
	{ SRB_STATUS_SELECTION_TIMEOUT,	"Selection Timeout"},
	{ SRB_STATUS_COMMAND_TIMEOUT,	"Command Timeout"},
	{ SRB_STATUS_MESSAGE_REJECTED,	"Message Rejected"},
	{ SRB_STATUS_BUS_RESET,		"Bus Reset"},
	{ SRB_STATUS_PARITY_ERROR,	"Parity Error"},
	{ SRB_STATUS_REQUEST_SENSE_FAILED,"Request Sense Failed"},
	{ SRB_STATUS_NO_HBA,		"No HBA"},
	{ SRB_STATUS_DATA_OVERRUN,	"Data Overrun/Data Underrun"},
	{ SRB_STATUS_UNEXPECTED_BUS_FREE,"Unexpected Bus Free"},
	{ SRB_STATUS_PHASE_SEQUENCE_FAILURE,"Phase Error"},
	{ SRB_STATUS_BAD_SRB_BLOCK_LENGTH,"Bad Srb Block Length"},
	{ SRB_STATUS_REQUEST_FLUSHED,	"Request Flushed"},
	{ SRB_STATUS_DELAYED_RETRY,	"Delayed Retry"},
	{ SRB_STATUS_INVALID_LUN,	"Invalid LUN"},
	{ SRB_STATUS_INVALID_TARGET_ID,	"Invalid TARGET ID"},
	{ SRB_STATUS_BAD_FUNCTION,	"Bad Function"},
	{ SRB_STATUS_ERROR_RECOVERY,	"Error Recovery"},
	{ SRB_STATUS_NOT_STARTED,	"Not Started"},
	{ SRB_STATUS_NOT_IN_USE,	"Not In Use"},
    	{ SRB_STATUS_FORCE_ABORT,	"Force Abort"},
	{ SRB_STATUS_DOMAIN_VALIDATION_FAIL,"Domain Validation Failure"},
	{ 0xff,				"Unknown Error"}
};

char *aac_get_status_string(u32 status)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(srb_status_info); i++)
		if (srb_status_info[i].status == status)
			return srb_status_info[i].str;

	return "Bad Status Code";
}

#endif
