// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	Adaptec AAC series RAID controller driver
 *	(c) Copyright 2001 Red Hat Inc.
 *
 * based on the old aacraid driver that is..
 * Adaptec aacraid device driver for Linux.
 *
 * Copyright (c) 2000-2010 Adaptec, Inc.
 *               2010-2015 PMC-Sierra, Inc. (aacraid@pmc-sierra.com)
 *		 2016-2017 Microsemi Corp. (aacraid@microsemi.com)
 *
 * Module Name:
 *  aachba.c
 *
 * Abstract: Contains Interfaces to manage IOs.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/completion.h>
#include <linux/blkdev.h>
#include <linux/uaccess.h>
#include <linux/highmem.h> /* For flush_kernel_dcache_page */
#include <linux/module.h>

#include <asm/unaligned.h>

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

#define SENCODE_NO_SENSE			0x00
#define SENCODE_END_OF_DATA			0x00
#define SENCODE_BECOMING_READY			0x04
#define SENCODE_INIT_CMD_REQUIRED		0x04
#define SENCODE_UNRECOVERED_READ_ERROR		0x11
#define SENCODE_PARAM_LIST_LENGTH_ERROR		0x1A
#define SENCODE_INVALID_COMMAND			0x20
#define SENCODE_LBA_OUT_OF_RANGE		0x21
#define SENCODE_INVALID_CDB_FIELD		0x24
#define SENCODE_LUN_NOT_SUPPORTED		0x25
#define SENCODE_INVALID_PARAM_FIELD		0x26
#define SENCODE_PARAM_NOT_SUPPORTED		0x26
#define SENCODE_PARAM_VALUE_INVALID		0x26
#define SENCODE_RESET_OCCURRED			0x29
#define SENCODE_LUN_NOT_SELF_CONFIGURED_YET	0x3E
#define SENCODE_INQUIRY_DATA_CHANGED		0x3F
#define SENCODE_SAVING_PARAMS_NOT_SUPPORTED	0x39
#define SENCODE_DIAGNOSTIC_FAILURE		0x40
#define SENCODE_INTERNAL_TARGET_FAILURE		0x44
#define SENCODE_INVALID_MESSAGE_ERROR		0x49
#define SENCODE_LUN_FAILED_SELF_CONFIG		0x4c
#define SENCODE_OVERLAPPED_COMMAND		0x4E

/*
 *	Additional sense codes
 */

#define ASENCODE_NO_SENSE			0x00
#define ASENCODE_END_OF_DATA			0x05
#define ASENCODE_BECOMING_READY			0x01
#define ASENCODE_INIT_CMD_REQUIRED		0x02
#define ASENCODE_PARAM_LIST_LENGTH_ERROR	0x00
#define ASENCODE_INVALID_COMMAND		0x00
#define ASENCODE_LBA_OUT_OF_RANGE		0x00
#define ASENCODE_INVALID_CDB_FIELD		0x00
#define ASENCODE_LUN_NOT_SUPPORTED		0x00
#define ASENCODE_INVALID_PARAM_FIELD		0x00
#define ASENCODE_PARAM_NOT_SUPPORTED		0x01
#define ASENCODE_PARAM_VALUE_INVALID		0x02
#define ASENCODE_RESET_OCCURRED			0x00
#define ASENCODE_LUN_NOT_SELF_CONFIGURED_YET	0x00
#define ASENCODE_INQUIRY_DATA_CHANGED		0x03
#define ASENCODE_SAVING_PARAMS_NOT_SUPPORTED	0x00
#define ASENCODE_DIAGNOSTIC_FAILURE		0x80
#define ASENCODE_INTERNAL_TARGET_FAILURE	0x00
#define ASENCODE_INVALID_MESSAGE_ERROR		0x00
#define ASENCODE_LUN_FAILED_SELF_CONFIG		0x00
#define ASENCODE_OVERLAPPED_COMMAND		0x00

#define BYTE0(x) (unsigned char)(x)
#define BYTE1(x) (unsigned char)((x) >> 8)
#define BYTE2(x) (unsigned char)((x) >> 16)
#define BYTE3(x) (unsigned char)((x) >> 24)

/* MODE_SENSE data format */
typedef struct {
	struct {
		u8	data_length;
		u8	med_type;
		u8	dev_par;
		u8	bd_length;
	} __attribute__((packed)) hd;
	struct {
		u8	dens_code;
		u8	block_count[3];
		u8	reserved;
		u8	block_length[3];
	} __attribute__((packed)) bd;
		u8	mpc_buf[3];
} __attribute__((packed)) aac_modep_data;

/* MODE_SENSE_10 data format */
typedef struct {
	struct {
		u8	data_length[2];
		u8	med_type;
		u8	dev_par;
		u8	rsrvd[2];
		u8	bd_length[2];
	} __attribute__((packed)) hd;
	struct {
		u8	dens_code;
		u8	block_count[3];
		u8	reserved;
		u8	block_length[3];
	} __attribute__((packed)) bd;
		u8	mpc_buf[3];
} __attribute__((packed)) aac_modep10_data;

/*------------------------------------------------------------------------------
 *              S T R U C T S / T Y P E D E F S
 *----------------------------------------------------------------------------*/
/* SCSI inquiry data */
struct inquiry_data {
	u8 inqd_pdt;	/* Peripheral qualifier | Peripheral Device Type */
	u8 inqd_dtq;	/* RMB | Device Type Qualifier */
	u8 inqd_ver;	/* ISO version | ECMA version | ANSI-approved version */
	u8 inqd_rdf;	/* AENC | TrmIOP | Response data format */
	u8 inqd_len;	/* Additional length (n-4) */
	u8 inqd_pad1[2];/* Reserved - must be zero */
	u8 inqd_pad2;	/* RelAdr | WBus32 | WBus16 |  Sync  | Linked |Reserved| CmdQue | SftRe */
	u8 inqd_vid[8];	/* Vendor ID */
	u8 inqd_pid[16];/* Product ID */
	u8 inqd_prl[4];	/* Product Revision Level */
};

/* Added for VPD 0x83 */
struct  tvpd_id_descriptor_type_1 {
	u8 codeset:4;		/* VPD_CODE_SET */
	u8 reserved:4;
	u8 identifiertype:4;	/* VPD_IDENTIFIER_TYPE */
	u8 reserved2:4;
	u8 reserved3;
	u8 identifierlength;
	u8 venid[8];
	u8 productid[16];
	u8 serialnumber[8];	/* SN in ASCII */

};

struct tvpd_id_descriptor_type_2 {
	u8 codeset:4;		/* VPD_CODE_SET */
	u8 reserved:4;
	u8 identifiertype:4;	/* VPD_IDENTIFIER_TYPE */
	u8 reserved2:4;
	u8 reserved3;
	u8 identifierlength;
	struct teu64id {
		u32 Serial;
		 /* The serial number supposed to be 40 bits,
		  * bit we only support 32, so make the last byte zero. */
		u8 reserved;
		u8 venid[3];
	} eu64id;

};

struct tvpd_id_descriptor_type_3 {
	u8 codeset : 4;          /* VPD_CODE_SET */
	u8 reserved : 4;
	u8 identifiertype : 4;   /* VPD_IDENTIFIER_TYPE */
	u8 reserved2 : 4;
	u8 reserved3;
	u8 identifierlength;
	u8 Identifier[16];
};

struct tvpd_page83 {
	u8 DeviceType:5;
	u8 DeviceTypeQualifier:3;
	u8 PageCode;
	u8 reserved;
	u8 PageLength;
	struct tvpd_id_descriptor_type_1 type1;
	struct tvpd_id_descriptor_type_2 type2;
	struct tvpd_id_descriptor_type_3 type3;
};

/*
 *              M O D U L E   G L O B A L S
 */

static long aac_build_sg(struct scsi_cmnd *scsicmd, struct sgmap *sgmap);
static long aac_build_sg64(struct scsi_cmnd *scsicmd, struct sgmap64 *psg);
static long aac_build_sgraw(struct scsi_cmnd *scsicmd, struct sgmapraw *psg);
static long aac_build_sgraw2(struct scsi_cmnd *scsicmd,
				struct aac_raw_io2 *rio2, int sg_max);
static long aac_build_sghba(struct scsi_cmnd *scsicmd,
				struct aac_hba_cmd_req *hbacmd,
				int sg_max, u64 sg_address);
static int aac_convert_sgraw2(struct aac_raw_io2 *rio2,
				int pages, int nseg, int nseg_new);
static int aac_send_srb_fib(struct scsi_cmnd* scsicmd);
static int aac_send_hba_fib(struct scsi_cmnd *scsicmd);
#ifdef AAC_DETAILED_STATUS_INFO
static char *aac_get_status_string(u32 status);
#endif

/*
 *	Non dasd selection is handled entirely in aachba now
 */

static int nondasd = -1;
static int aac_cache = 2;	/* WCE=0 to avoid performance problems */
static int dacmode = -1;
int aac_msi;
int aac_commit = -1;
int startup_timeout = 180;
int aif_timeout = 120;
int aac_sync_mode;  /* Only Sync. transfer - disabled */
int aac_convert_sgl = 1;	/* convert non-conformable s/g list - enabled */

module_param(aac_sync_mode, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(aac_sync_mode, "Force sync. transfer mode"
	" 0=off, 1=on");
module_param(aac_convert_sgl, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(aac_convert_sgl, "Convert non-conformable s/g list"
	" 0=off, 1=on");
module_param(nondasd, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(nondasd, "Control scanning of hba for nondasd devices."
	" 0=off, 1=on");
module_param_named(cache, aac_cache, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(cache, "Disable Queue Flush commands:\n"
	"\tbit 0 - Disable FUA in WRITE SCSI commands\n"
	"\tbit 1 - Disable SYNCHRONIZE_CACHE SCSI command\n"
	"\tbit 2 - Disable only if Battery is protecting Cache");
module_param(dacmode, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(dacmode, "Control whether dma addressing is using 64 bit DAC."
	" 0=off, 1=on");
module_param_named(commit, aac_commit, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(commit, "Control whether a COMMIT_CONFIG is issued to the"
	" adapter for foreign arrays.\n"
	"This is typically needed in systems that do not have a BIOS."
	" 0=off, 1=on");
module_param_named(msi, aac_msi, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(msi, "IRQ handling."
	" 0=PIC(default), 1=MSI, 2=MSI-X)");
module_param(startup_timeout, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(startup_timeout, "The duration of time in seconds to wait for"
	" adapter to have it's kernel up and\n"
	"running. This is typically adjusted for large systems that do not"
	" have a BIOS.");
module_param(aif_timeout, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(aif_timeout, "The duration of time in seconds to wait for"
	" applications to pick up AIFs before\n"
	"deregistering them. This is typically adjusted for heavily burdened"
	" systems.");

int aac_fib_dump;
module_param(aac_fib_dump, int, 0644);
MODULE_PARM_DESC(aac_fib_dump, "Dump controller fibs prior to IOP_RESET 0=off, 1=on");

int numacb = -1;
module_param(numacb, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(numacb, "Request a limit to the number of adapter control"
	" blocks (FIB) allocated. Valid values are 512 and down. Default is"
	" to use suggestion from Firmware.");

int acbsize = -1;
module_param(acbsize, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(acbsize, "Request a specific adapter control block (FIB)"
	" size. Valid values are 512, 2048, 4096 and 8192. Default is to use"
	" suggestion from Firmware.");

int update_interval = 30 * 60;
module_param(update_interval, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(update_interval, "Interval in seconds between time sync"
	" updates issued to adapter.");

int check_interval = 60;
module_param(check_interval, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(check_interval, "Interval in seconds between adapter health"
	" checks.");

int aac_check_reset = 1;
module_param_named(check_reset, aac_check_reset, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(check_reset, "If adapter fails health check, reset the"
	" adapter. a value of -1 forces the reset to adapters programmed to"
	" ignore it.");

int expose_physicals = -1;
module_param(expose_physicals, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(expose_physicals, "Expose physical components of the arrays."
	" -1=protect 0=off, 1=on");

int aac_reset_devices;
module_param_named(reset_devices, aac_reset_devices, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(reset_devices, "Force an adapter reset at initialization.");

int aac_wwn = 1;
module_param_named(wwn, aac_wwn, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(wwn, "Select a WWN type for the arrays:\n"
	"\t0 - Disable\n"
	"\t1 - Array Meta Data Signature (default)\n"
	"\t2 - Adapter Serial Number");


static inline int aac_valid_context(struct scsi_cmnd *scsicmd,
		struct fib *fibptr) {
	struct scsi_device *device;

	if (unlikely(!scsicmd || !scsicmd->scsi_done)) {
		dprintk((KERN_WARNING "aac_valid_context: scsi command corrupt\n"));
		aac_fib_complete(fibptr);
		return 0;
	}
	scsicmd->SCp.phase = AAC_OWNER_MIDLEVEL;
	device = scsicmd->device;
	if (unlikely(!device)) {
		dprintk((KERN_WARNING "aac_valid_context: scsi device corrupt\n"));
		aac_fib_complete(fibptr);
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
	if (status < 0) {
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
	/* Do not set XferState to zero unless receives a response from F/W */
	if (status >= 0)
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
			/* Do not set XferState to zero unless
			 * receives a response from F/W */
			if (status >= 0)
				aac_fib_complete(fibptr);
		} else if (aac_commit == 0) {
			printk(KERN_WARNING
			  "aac_get_config_status: Foreign device configurations are being ignored\n");
		}
	}
	/* FIB should be freed only after getting the response from the F/W */
	if (status != -ERESTARTSYS)
		aac_fib_free(fibptr);
	return status;
}

static void aac_expose_phy_device(struct scsi_cmnd *scsicmd)
{
	char inq_data;
	scsi_sg_copy_to_buffer(scsicmd,  &inq_data, sizeof(inq_data));
	if ((inq_data & 0x20) && (inq_data & 0x1f) == TYPE_DISK) {
		inq_data &= 0xdf;
		scsi_sg_copy_from_buffer(scsicmd, &inq_data, sizeof(inq_data));
	}
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
		if (fibptr->dev->supplement_adapter_info.supported_options2 &
		    AAC_OPTION_SUPPORTED_240_VOLUMES) {
			maximum_num_containers =
				le32_to_cpu(dresp->MaxSimpleVolumes);
		}
		aac_fib_complete(fibptr);
	}
	/* FIB should be freed only after getting the response from the F/W */
	if (status != -ERESTARTSYS)
		aac_fib_free(fibptr);

	if (maximum_num_containers < MAXIMUM_NUM_CONTAINERS)
		maximum_num_containers = MAXIMUM_NUM_CONTAINERS;
	if (dev->fsa_dev == NULL ||
		dev->maximum_num_containers != maximum_num_containers) {

		fsa_dev_ptr = dev->fsa_dev;

		dev->fsa_dev = kcalloc(maximum_num_containers,
					sizeof(*fsa_dev_ptr), GFP_KERNEL);

		kfree(fsa_dev_ptr);
		fsa_dev_ptr = NULL;


		if (!dev->fsa_dev)
			return -ENOMEM;

		dev->maximum_num_containers = maximum_num_containers;
	}
	for (index = 0; index < dev->maximum_num_containers; index++) {
		dev->fsa_dev[index].devname[0] = '\0';
		dev->fsa_dev[index].valid = 0;

		status = aac_probe_container(dev, index);

		if (status < 0) {
			printk(KERN_WARNING "aac_get_containers: SendFIB failed.\n");
			break;
		}
	}
	return status;
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
		int data_size = FIELD_SIZEOF(struct aac_get_name_resp, data);

		sp[data_size - 1] = '\0';
		while (*sp == ' ')
			++sp;
		if (*sp) {
			struct inquiry_data inq;
			char d[sizeof(((struct inquiry_data *)NULL)->inqd_pid)];
			int count = sizeof(d);
			char *dp = d;
			do {
				*dp++ = (*sp) ? *sp++ : ' ';
			} while (--count > 0);

			scsi_sg_copy_to_buffer(scsicmd, &inq, sizeof(inq));
			memcpy(inq.inqd_pid, d, sizeof(d));
			scsi_sg_copy_from_buffer(scsicmd, &inq, sizeof(inq));
		}
	}

	scsicmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8 | SAM_STAT_GOOD;

	aac_fib_complete(fibptr);
	scsicmd->scsi_done(scsicmd);
}

/**
 *	aac_get_container_name	-	get container name, none blocking.
 */
static int aac_get_container_name(struct scsi_cmnd * scsicmd)
{
	int status;
	int data_size;
	struct aac_get_name *dinfo;
	struct fib * cmd_fibcontext;
	struct aac_dev * dev;

	dev = (struct aac_dev *)scsicmd->device->host->hostdata;

	data_size = FIELD_SIZEOF(struct aac_get_name_resp, data);

	cmd_fibcontext = aac_fib_alloc_tag(dev, scsicmd);

	aac_fib_init(cmd_fibcontext);
	dinfo = (struct aac_get_name *) fib_data(cmd_fibcontext);
	scsicmd->SCp.phase = AAC_OWNER_FIRMWARE;

	dinfo->command = cpu_to_le32(VM_ContainerConfig);
	dinfo->type = cpu_to_le32(CT_READ_NAME);
	dinfo->cid = cpu_to_le32(scmd_id(scsicmd));
	dinfo->count = cpu_to_le32(data_size - 1);

	status = aac_fib_send(ContainerCommand,
		  cmd_fibcontext,
		  sizeof(struct aac_get_name_resp),
		  FsaNormal,
		  0, 1,
		  (fib_callback)get_container_name_callback,
		  (void *) scsicmd);

	/*
	 *	Check that the command queued to the controller
	 */
	if (status == -EINPROGRESS)
		return 0;

	printk(KERN_WARNING "aac_get_container_name: aac_fib_send failed with status: %d.\n", status);
	aac_fib_complete(cmd_fibcontext);
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
	int i;


	if (!aac_valid_context(scsicmd, fibptr))
		return;

	scsicmd->SCp.Status = 0;
	fsa_dev_ptr = fibptr->dev->fsa_dev;
	if (fsa_dev_ptr) {
		struct aac_mount * dresp = (struct aac_mount *) fib_data(fibptr);
		__le32 sup_options2;

		fsa_dev_ptr += scmd_id(scsicmd);
		sup_options2 =
			fibptr->dev->supplement_adapter_info.supported_options2;

		if ((le32_to_cpu(dresp->status) == ST_OK) &&
		    (le32_to_cpu(dresp->mnt[0].vol) != CT_NONE) &&
		    (le32_to_cpu(dresp->mnt[0].state) != FSCS_HIDDEN)) {
			if (!(sup_options2 & AAC_OPTION_VARIABLE_BLOCK_SIZE)) {
				dresp->mnt[0].fileinfo.bdevinfo.block_size = 0x200;
				fsa_dev_ptr->block_size = 0x200;
			} else {
				fsa_dev_ptr->block_size =
					le32_to_cpu(dresp->mnt[0].fileinfo.bdevinfo.block_size);
			}
			for (i = 0; i < 16; i++)
				fsa_dev_ptr->identifier[i] =
					dresp->mnt[0].fileinfo.bdevinfo
								.identifier[i];
			fsa_dev_ptr->valid = 1;
			/* sense_key holds the current state of the spin-up */
			if (dresp->mnt[0].state & cpu_to_le32(FSCS_NOT_READY))
				fsa_dev_ptr->sense_data.sense_key = NOT_READY;
			else if (fsa_dev_ptr->sense_data.sense_key == NOT_READY)
				fsa_dev_ptr->sense_data.sense_key = NO_SENSE;
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
	if (!aac_supports_2T(fibptr->dev)) {
		dresp->mnt[0].capacityhigh = 0;
		if ((le32_to_cpu(dresp->status) == ST_OK) &&
			(le32_to_cpu(dresp->mnt[0].vol) != CT_NONE)) {
			_aac_probe_container2(context, fibptr);
			return;
		}
	}
	scsicmd = (struct scsi_cmnd *) context;

	if (!aac_valid_context(scsicmd, fibptr))
		return;

	aac_fib_init(fibptr);

	dinfo = (struct aac_query_mount *)fib_data(fibptr);

	if (fibptr->dev->supplement_adapter_info.supported_options2 &
	    AAC_OPTION_VARIABLE_BLOCK_SIZE)
		dinfo->command = cpu_to_le32(VM_NameServeAllBlk);
	else
		dinfo->command = cpu_to_le32(VM_NameServe64);

	dinfo->count = cpu_to_le32(scmd_id(scsicmd));
	dinfo->type = cpu_to_le32(FT_FILESYS);
	scsicmd->SCp.phase = AAC_OWNER_FIRMWARE;

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
	if (status < 0 && status != -EINPROGRESS) {
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

		if (fibptr->dev->supplement_adapter_info.supported_options2 &
		    AAC_OPTION_VARIABLE_BLOCK_SIZE)
			dinfo->command = cpu_to_le32(VM_NameServeAllBlk);
		else
			dinfo->command = cpu_to_le32(VM_NameServe);

		dinfo->count = cpu_to_le32(scmd_id(scsicmd));
		dinfo->type = cpu_to_le32(FT_FILESYS);
		scsicmd->SCp.ptr = (char *)callback;
		scsicmd->SCp.phase = AAC_OWNER_FIRMWARE;

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
		if (status == -EINPROGRESS)
			return 0;

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
 *	Copy a String from one location to another
 *	without copying \0
 */

static void inqstrcpy(char *a, char *b)
{

	while (*a != (char)0)
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

char * get_container_type(unsigned tindex)
{
	if (tindex >= ARRAY_SIZE(container_types))
		tindex = ARRAY_SIZE(container_types) - 1;
	return container_types[tindex];
}

/* Function: setinqstr
 *
 * Arguments: [1] pointer to void [1] int
 *
 * Purpose: Sets SCSI inquiry data strings for vendor, product
 * and revision level. Allows strings to be set in platform dependent
 * files instead of in OS dependent driver source.
 */

static void setinqstr(struct aac_dev *dev, void *data, int tindex)
{
	struct scsi_inq *str;
	struct aac_supplement_adapter_info *sup_adap_info;

	sup_adap_info = &dev->supplement_adapter_info;
	str = (struct scsi_inq *)(data); /* cast data to scsi inq block */
	memset(str, ' ', sizeof(*str));

	if (sup_adap_info->adapter_type_text[0]) {
		int c;
		char *cp;
		char *cname = kmemdup(sup_adap_info->adapter_type_text,
				sizeof(sup_adap_info->adapter_type_text),
								GFP_ATOMIC);
		if (!cname)
			return;

		cp = cname;
		if ((cp[0] == 'A') && (cp[1] == 'O') && (cp[2] == 'C'))
			inqstrcpy("SMC", str->vid);
		else {
			c = sizeof(str->vid);
			while (*cp && *cp != ' ' && --c)
				++cp;
			c = *cp;
			*cp = '\0';
			inqstrcpy(cname, str->vid);
			*cp = c;
			while (*cp && *cp != ' ')
				++cp;
		}
		while (*cp == ' ')
			++cp;
		/* last six chars reserved for vol type */
		if (strlen(cp) > sizeof(str->pid))
			cp[sizeof(str->pid)] = '\0';
		inqstrcpy (cp, str->pid);

		kfree(cname);
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

static void build_vpd83_type3(struct tvpd_page83 *vpdpage83data,
		struct aac_dev *dev, struct scsi_cmnd *scsicmd)
{
	int container;

	vpdpage83data->type3.codeset = 1;
	vpdpage83data->type3.identifiertype = 3;
	vpdpage83data->type3.identifierlength = sizeof(vpdpage83data->type3)
			- 4;

	for (container = 0; container < dev->maximum_num_containers;
			container++) {

		if (scmd_id(scsicmd) == container) {
			memcpy(vpdpage83data->type3.Identifier,
					dev->fsa_dev[container].identifier,
					16);
			break;
		}
	}
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
		/*Check to see if it's for VPD 0x83 or 0x80 */
		if (scsicmd->cmnd[2] == 0x83) {
			/* vpd page 0x83 - Device Identification Page */
			struct aac_dev *dev;
			int i;
			struct tvpd_page83 vpdpage83data;

			dev = (struct aac_dev *)scsicmd->device->host->hostdata;

			memset(((u8 *)&vpdpage83data), 0,
			       sizeof(vpdpage83data));

			/* DIRECT_ACCESS_DEVIC */
			vpdpage83data.DeviceType = 0;
			/* DEVICE_CONNECTED */
			vpdpage83data.DeviceTypeQualifier = 0;
			/* VPD_DEVICE_IDENTIFIERS */
			vpdpage83data.PageCode = 0x83;
			vpdpage83data.reserved = 0;
			vpdpage83data.PageLength =
				sizeof(vpdpage83data.type1) +
				sizeof(vpdpage83data.type2);

			/* VPD 83 Type 3 is not supported for ARC */
			if (dev->sa_firmware)
				vpdpage83data.PageLength +=
				sizeof(vpdpage83data.type3);

			/* T10 Vendor Identifier Field Format */
			/* VpdcodesetAscii */
			vpdpage83data.type1.codeset = 2;
			/* VpdIdentifierTypeVendorId */
			vpdpage83data.type1.identifiertype = 1;
			vpdpage83data.type1.identifierlength =
				sizeof(vpdpage83data.type1) - 4;

			/* "ADAPTEC " for adaptec */
			memcpy(vpdpage83data.type1.venid,
				"ADAPTEC ",
				sizeof(vpdpage83data.type1.venid));
			memcpy(vpdpage83data.type1.productid,
				"ARRAY           ",
				sizeof(
				vpdpage83data.type1.productid));

			/* Convert to ascii based serial number.
			 * The LSB is the the end.
			 */
			for (i = 0; i < 8; i++) {
				u8 temp =
					(u8)((get_serial_reply->uid >> ((7 - i) * 4)) & 0xF);
				if (temp  > 0x9) {
					vpdpage83data.type1.serialnumber[i] =
							'A' + (temp - 0xA);
				} else {
					vpdpage83data.type1.serialnumber[i] =
							'0' + temp;
				}
			}

			/* VpdCodeSetBinary */
			vpdpage83data.type2.codeset = 1;
			/* VpdidentifiertypeEUI64 */
			vpdpage83data.type2.identifiertype = 2;
			vpdpage83data.type2.identifierlength =
				sizeof(vpdpage83data.type2) - 4;

			vpdpage83data.type2.eu64id.venid[0] = 0xD0;
			vpdpage83data.type2.eu64id.venid[1] = 0;
			vpdpage83data.type2.eu64id.venid[2] = 0;

			vpdpage83data.type2.eu64id.Serial =
							get_serial_reply->uid;
			vpdpage83data.type2.eu64id.reserved = 0;

			/*
			 * VpdIdentifierTypeFCPHName
			 * VPD 0x83 Type 3 not supported for ARC
			 */
			if (dev->sa_firmware) {
				build_vpd83_type3(&vpdpage83data,
						dev, scsicmd);
			}

			/* Move the inquiry data to the response buffer. */
			scsi_sg_copy_from_buffer(scsicmd, &vpdpage83data,
						 sizeof(vpdpage83data));
		} else {
			/* It must be for VPD 0x80 */
			char sp[13];
			/* EVPD bit set */
			sp[0] = INQD_PDT_DA;
			sp[1] = scsicmd->cmnd[2];
			sp[2] = 0;
			sp[3] = snprintf(sp+4, sizeof(sp)-4, "%08X",
				le32_to_cpu(get_serial_reply->uid));
			scsi_sg_copy_from_buffer(scsicmd, sp,
						 sizeof(sp));
		}
	}

	scsicmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8 | SAM_STAT_GOOD;

	aac_fib_complete(fibptr);
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

	cmd_fibcontext = aac_fib_alloc_tag(dev, scsicmd);

	aac_fib_init(cmd_fibcontext);
	dinfo = (struct aac_get_serial *) fib_data(cmd_fibcontext);

	dinfo->command = cpu_to_le32(VM_ContainerConfig);
	dinfo->type = cpu_to_le32(CT_CID_TO_32BITS_UID);
	dinfo->cid = cpu_to_le32(scmd_id(scsicmd));
	scsicmd->SCp.phase = AAC_OWNER_FIRMWARE;

	status = aac_fib_send(ContainerCommand,
		  cmd_fibcontext,
		  sizeof(struct aac_get_serial_resp),
		  FsaNormal,
		  0, 1,
		  (fib_callback) get_container_serial_callback,
		  (void *) scsicmd);

	/*
	 *	Check that the command queued to the controller
	 */
	if (status == -EINPROGRESS)
		return 0;

	printk(KERN_WARNING "aac_get_container_serial: aac_fib_send failed with status: %d.\n", status);
	aac_fib_complete(cmd_fibcontext);
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

static inline void set_sense(struct sense_data *sense_data, u8 sense_key,
	u8 sense_code, u8 a_sense_code, u8 bit_pointer, u16 field_pointer)
{
	u8 *sense_buf = (u8 *)sense_data;
	/* Sense data valid, err code 70h */
	sense_buf[0] = 0x70; /* No info field */
	sense_buf[1] = 0;	/* Segment number, always zero */

	sense_buf[2] = sense_key;	/* Sense key */

	sense_buf[12] = sense_code;	/* Additional sense code */
	sense_buf[13] = a_sense_code;	/* Additional sense code qualifier */

	if (sense_key == ILLEGAL_REQUEST) {
		sense_buf[7] = 10;	/* Additional sense length */

		sense_buf[15] = bit_pointer;
		/* Illegal parameter is in the parameter block */
		if (sense_code == SENCODE_INVALID_CDB_FIELD)
			sense_buf[15] |= 0xc0;/* Std sense key specific field */
		/* Illegal parameter is in the CDB block */
		sense_buf[16] = field_pointer >> 8;	/* MSB */
		sense_buf[17] = field_pointer;		/* LSB */
	} else
		sense_buf[7] = 6;	/* Additional sense length */
}

static int aac_bounds_32(struct aac_dev * dev, struct scsi_cmnd * cmd, u64 lba)
{
	if (lba & 0xffffffff00000000LL) {
		int cid = scmd_id(cmd);
		dprintk((KERN_DEBUG "aacraid: Illegal lba\n"));
		cmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8 |
			SAM_STAT_CHECK_CONDITION;
		set_sense(&dev->fsa_dev[cid].sense_data,
		  HARDWARE_ERROR, SENCODE_INTERNAL_TARGET_FAILURE,
		  ASENCODE_INTERNAL_TARGET_FAILURE, 0, 0);
		memcpy(cmd->sense_buffer, &dev->fsa_dev[cid].sense_data,
		       min_t(size_t, sizeof(dev->fsa_dev[cid].sense_data),
			     SCSI_SENSE_BUFFERSIZE));
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
	struct aac_dev *dev = fib->dev;
	u16 fibsize, command;
	long ret;

	aac_fib_init(fib);
	if ((dev->comm_interface == AAC_COMM_MESSAGE_TYPE2 ||
		dev->comm_interface == AAC_COMM_MESSAGE_TYPE3) &&
		!dev->sync_mode) {
		struct aac_raw_io2 *readcmd2;
		readcmd2 = (struct aac_raw_io2 *) fib_data(fib);
		memset(readcmd2, 0, sizeof(struct aac_raw_io2));
		readcmd2->blockLow = cpu_to_le32((u32)(lba&0xffffffff));
		readcmd2->blockHigh = cpu_to_le32((u32)((lba&0xffffffff00000000LL)>>32));
		readcmd2->byteCount = cpu_to_le32(count *
			dev->fsa_dev[scmd_id(cmd)].block_size);
		readcmd2->cid = cpu_to_le16(scmd_id(cmd));
		readcmd2->flags = cpu_to_le16(RIO2_IO_TYPE_READ);
		ret = aac_build_sgraw2(cmd, readcmd2,
				dev->scsi_host_ptr->sg_tablesize);
		if (ret < 0)
			return ret;
		command = ContainerRawIo2;
		fibsize = sizeof(struct aac_raw_io2) +
			((le32_to_cpu(readcmd2->sgeCnt)-1) * sizeof(struct sge_ieee1212));
	} else {
		struct aac_raw_io *readcmd;
		readcmd = (struct aac_raw_io *) fib_data(fib);
		readcmd->block[0] = cpu_to_le32((u32)(lba&0xffffffff));
		readcmd->block[1] = cpu_to_le32((u32)((lba&0xffffffff00000000LL)>>32));
		readcmd->count = cpu_to_le32(count *
			dev->fsa_dev[scmd_id(cmd)].block_size);
		readcmd->cid = cpu_to_le16(scmd_id(cmd));
		readcmd->flags = cpu_to_le16(RIO_TYPE_READ);
		readcmd->bpTotal = 0;
		readcmd->bpComplete = 0;
		ret = aac_build_sgraw(cmd, &readcmd->sg);
		if (ret < 0)
			return ret;
		command = ContainerRawIo;
		fibsize = sizeof(struct aac_raw_io) +
			((le32_to_cpu(readcmd->sg.count)-1) * sizeof(struct sgentryraw));
	}

	BUG_ON(fibsize > (fib->dev->max_fib_size - sizeof(struct aac_fibhdr)));
	/*
	 *	Now send the Fib to the adapter
	 */
	return aac_fib_send(command,
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
	long ret;

	aac_fib_init(fib);
	readcmd = (struct aac_read64 *) fib_data(fib);
	readcmd->command = cpu_to_le32(VM_CtHostRead64);
	readcmd->cid = cpu_to_le16(scmd_id(cmd));
	readcmd->sector_count = cpu_to_le16(count);
	readcmd->block = cpu_to_le32((u32)(lba&0xffffffff));
	readcmd->pad   = 0;
	readcmd->flags = 0;

	ret = aac_build_sg64(cmd, &readcmd->sg);
	if (ret < 0)
		return ret;
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
	struct aac_dev *dev = fib->dev;
	long ret;

	aac_fib_init(fib);
	readcmd = (struct aac_read *) fib_data(fib);
	readcmd->command = cpu_to_le32(VM_CtBlockRead);
	readcmd->cid = cpu_to_le32(scmd_id(cmd));
	readcmd->block = cpu_to_le32((u32)(lba&0xffffffff));
	readcmd->count = cpu_to_le32(count *
		dev->fsa_dev[scmd_id(cmd)].block_size);

	ret = aac_build_sg(cmd, &readcmd->sg);
	if (ret < 0)
		return ret;
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
	struct aac_dev *dev = fib->dev;
	u16 fibsize, command;
	long ret;

	aac_fib_init(fib);
	if ((dev->comm_interface == AAC_COMM_MESSAGE_TYPE2 ||
		dev->comm_interface == AAC_COMM_MESSAGE_TYPE3) &&
		!dev->sync_mode) {
		struct aac_raw_io2 *writecmd2;
		writecmd2 = (struct aac_raw_io2 *) fib_data(fib);
		memset(writecmd2, 0, sizeof(struct aac_raw_io2));
		writecmd2->blockLow = cpu_to_le32((u32)(lba&0xffffffff));
		writecmd2->blockHigh = cpu_to_le32((u32)((lba&0xffffffff00000000LL)>>32));
		writecmd2->byteCount = cpu_to_le32(count *
			dev->fsa_dev[scmd_id(cmd)].block_size);
		writecmd2->cid = cpu_to_le16(scmd_id(cmd));
		writecmd2->flags = (fua && ((aac_cache & 5) != 1) &&
						   (((aac_cache & 5) != 5) || !fib->dev->cache_protected)) ?
			cpu_to_le16(RIO2_IO_TYPE_WRITE|RIO2_IO_SUREWRITE) :
			cpu_to_le16(RIO2_IO_TYPE_WRITE);
		ret = aac_build_sgraw2(cmd, writecmd2,
				dev->scsi_host_ptr->sg_tablesize);
		if (ret < 0)
			return ret;
		command = ContainerRawIo2;
		fibsize = sizeof(struct aac_raw_io2) +
			((le32_to_cpu(writecmd2->sgeCnt)-1) * sizeof(struct sge_ieee1212));
	} else {
		struct aac_raw_io *writecmd;
		writecmd = (struct aac_raw_io *) fib_data(fib);
		writecmd->block[0] = cpu_to_le32((u32)(lba&0xffffffff));
		writecmd->block[1] = cpu_to_le32((u32)((lba&0xffffffff00000000LL)>>32));
		writecmd->count = cpu_to_le32(count *
			dev->fsa_dev[scmd_id(cmd)].block_size);
		writecmd->cid = cpu_to_le16(scmd_id(cmd));
		writecmd->flags = (fua && ((aac_cache & 5) != 1) &&
						   (((aac_cache & 5) != 5) || !fib->dev->cache_protected)) ?
			cpu_to_le16(RIO_TYPE_WRITE|RIO_SUREWRITE) :
			cpu_to_le16(RIO_TYPE_WRITE);
		writecmd->bpTotal = 0;
		writecmd->bpComplete = 0;
		ret = aac_build_sgraw(cmd, &writecmd->sg);
		if (ret < 0)
			return ret;
		command = ContainerRawIo;
		fibsize = sizeof(struct aac_raw_io) +
			((le32_to_cpu(writecmd->sg.count)-1) * sizeof (struct sgentryraw));
	}

	BUG_ON(fibsize > (fib->dev->max_fib_size - sizeof(struct aac_fibhdr)));
	/*
	 *	Now send the Fib to the adapter
	 */
	return aac_fib_send(command,
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
	long ret;

	aac_fib_init(fib);
	writecmd = (struct aac_write64 *) fib_data(fib);
	writecmd->command = cpu_to_le32(VM_CtHostWrite64);
	writecmd->cid = cpu_to_le16(scmd_id(cmd));
	writecmd->sector_count = cpu_to_le16(count);
	writecmd->block = cpu_to_le32((u32)(lba&0xffffffff));
	writecmd->pad	= 0;
	writecmd->flags	= 0;

	ret = aac_build_sg64(cmd, &writecmd->sg);
	if (ret < 0)
		return ret;
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
	struct aac_dev *dev = fib->dev;
	long ret;

	aac_fib_init(fib);
	writecmd = (struct aac_write *) fib_data(fib);
	writecmd->command = cpu_to_le32(VM_CtBlockWrite);
	writecmd->cid = cpu_to_le32(scmd_id(cmd));
	writecmd->block = cpu_to_le32((u32)(lba&0xffffffff));
	writecmd->count = cpu_to_le32(count *
		dev->fsa_dev[scmd_id(cmd)].block_size);
	writecmd->sg.count = cpu_to_le32(1);
	/* ->stable is not used - it did mean which type of write */

	ret = aac_build_sg(cmd, &writecmd->sg);
	if (ret < 0)
		return ret;
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
	struct aac_dev *dev = fib->dev;

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
	timeout = cmd->request->timeout/HZ;
	if (timeout == 0)
		timeout = (dev->sa_firmware ? AAC_SA_TIMEOUT : AAC_ARC_TIMEOUT);
	srbcmd->timeout  = cpu_to_le32(timeout);  // timeout in seconds
	srbcmd->retry_limit = 0; /* Obsolete parameter */
	srbcmd->cdb_size = cpu_to_le32(cmd->cmd_len);
	return srbcmd;
}

static struct aac_hba_cmd_req *aac_construct_hbacmd(struct fib *fib,
							struct scsi_cmnd *cmd)
{
	struct aac_hba_cmd_req *hbacmd;
	struct aac_dev *dev;
	int bus, target;
	u64 address;

	dev = (struct aac_dev *)cmd->device->host->hostdata;

	hbacmd = (struct aac_hba_cmd_req *)fib->hw_fib_va;
	memset(hbacmd, 0, 96);	/* sizeof(*hbacmd) is not necessary */
	/* iu_type is a parameter of aac_hba_send */
	switch (cmd->sc_data_direction) {
	case DMA_TO_DEVICE:
		hbacmd->byte1 = 2;
		break;
	case DMA_FROM_DEVICE:
	case DMA_BIDIRECTIONAL:
		hbacmd->byte1 = 1;
		break;
	case DMA_NONE:
	default:
		break;
	}
	hbacmd->lun[1] = cpu_to_le32(cmd->device->lun);

	bus = aac_logical_to_phys(scmd_channel(cmd));
	target = scmd_id(cmd);
	hbacmd->it_nexus = dev->hba_map[bus][target].rmw_nexus;

	/* we fill in reply_qid later in aac_src_deliver_message */
	/* we fill in iu_type, request_id later in aac_hba_send */
	/* we fill in emb_data_desc_count later in aac_build_sghba */

	memcpy(hbacmd->cdb, cmd->cmnd, cmd->cmd_len);
	hbacmd->data_length = cpu_to_le32(scsi_bufflen(cmd));

	address = (u64)fib->hw_error_pa;
	hbacmd->error_ptr_hi = cpu_to_le32((u32)(address >> 32));
	hbacmd->error_ptr_lo = cpu_to_le32((u32)(address & 0xffffffff));
	hbacmd->error_length = cpu_to_le32(FW_ERROR_BUFFER_SIZE);

	return hbacmd;
}

static void aac_srb_callback(void *context, struct fib * fibptr);

static int aac_scsi_64(struct fib * fib, struct scsi_cmnd * cmd)
{
	u16 fibsize;
	struct aac_srb * srbcmd = aac_scsi_common(fib, cmd);
	long ret;

	ret = aac_build_sg64(cmd, (struct sgmap64 *) &srbcmd->sg);
	if (ret < 0)
		return ret;
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
	long ret;

	ret = aac_build_sg(cmd, (struct sgmap *)&srbcmd->sg);
	if (ret < 0)
		return ret;
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

static int aac_scsi_32_64(struct fib * fib, struct scsi_cmnd * cmd)
{
	if ((sizeof(dma_addr_t) > 4) && fib->dev->needs_dac &&
	    (fib->dev->adapter_info.options & AAC_OPT_SGMAP_HOST64))
		return FAILED;
	return aac_scsi_32(fib, cmd);
}

static int aac_adapter_hba(struct fib *fib, struct scsi_cmnd *cmd)
{
	struct aac_hba_cmd_req *hbacmd = aac_construct_hbacmd(fib, cmd);
	struct aac_dev *dev;
	long ret;

	dev = (struct aac_dev *)cmd->device->host->hostdata;

	ret = aac_build_sghba(cmd, hbacmd,
		dev->scsi_host_ptr->sg_tablesize, (u64)fib->hw_sgl_pa);
	if (ret < 0)
		return ret;

	/*
	 *	Now send the HBA command to the adapter
	 */
	fib->hbacmd_size = 64 + le32_to_cpu(hbacmd->emb_data_desc_count) *
		sizeof(struct aac_hba_sgl);

	return aac_hba_send(HBA_IU_TYPE_SCSI_CMD_REQ, fib,
				  (fib_callback) aac_hba_callback,
				  (void *) cmd);
}

static int aac_send_safw_bmic_cmd(struct aac_dev *dev,
	struct aac_srb_unit *srbu, void *xfer_buf, int xfer_len)
{
	struct fib	*fibptr;
	dma_addr_t	addr;
	int		rcode;
	int		fibsize;
	struct aac_srb	*srb;
	struct aac_srb_reply *srb_reply;
	struct sgmap64	*sg64;
	u32 vbus;
	u32 vid;

	if (!dev->sa_firmware)
		return 0;

	/* allocate FIB */
	fibptr = aac_fib_alloc(dev);
	if (!fibptr)
		return -ENOMEM;

	aac_fib_init(fibptr);
	fibptr->hw_fib_va->header.XferState &=
		~cpu_to_le32(FastResponseCapable);

	fibsize  = sizeof(struct aac_srb) - sizeof(struct sgentry) +
						sizeof(struct sgentry64);

	/* allocate DMA buffer for response */
	addr = dma_map_single(&dev->pdev->dev, xfer_buf, xfer_len,
							DMA_BIDIRECTIONAL);
	if (dma_mapping_error(&dev->pdev->dev, addr)) {
		rcode = -ENOMEM;
		goto fib_error;
	}

	srb = fib_data(fibptr);
	memcpy(srb, &srbu->srb, sizeof(struct aac_srb));

	vbus = (u32)le16_to_cpu(
			dev->supplement_adapter_info.virt_device_bus);
	vid  = (u32)le16_to_cpu(
			dev->supplement_adapter_info.virt_device_target);

	/* set the common request fields */
	srb->channel		= cpu_to_le32(vbus);
	srb->id			= cpu_to_le32(vid);
	srb->lun		= 0;
	srb->function		= cpu_to_le32(SRBF_ExecuteScsi);
	srb->timeout		= 0;
	srb->retry_limit	= 0;
	srb->cdb_size		= cpu_to_le32(16);
	srb->count		= cpu_to_le32(xfer_len);

	sg64 = (struct sgmap64 *)&srb->sg;
	sg64->count		= cpu_to_le32(1);
	sg64->sg[0].addr[1]	= cpu_to_le32(upper_32_bits(addr));
	sg64->sg[0].addr[0]	= cpu_to_le32(lower_32_bits(addr));
	sg64->sg[0].count	= cpu_to_le32(xfer_len);

	/*
	 * Copy the updated data for other dumping or other usage if needed
	 */
	memcpy(&srbu->srb, srb, sizeof(struct aac_srb));

	/* issue request to the controller */
	rcode = aac_fib_send(ScsiPortCommand64, fibptr, fibsize, FsaNormal,
					1, 1, NULL, NULL);

	if (rcode == -ERESTARTSYS)
		rcode = -ERESTART;

	if (unlikely(rcode < 0))
		goto bmic_error;

	srb_reply = (struct aac_srb_reply *)fib_data(fibptr);
	memcpy(&srbu->srb_reply, srb_reply, sizeof(struct aac_srb_reply));

bmic_error:
	dma_unmap_single(&dev->pdev->dev, addr, xfer_len, DMA_BIDIRECTIONAL);
fib_error:
	aac_fib_complete(fibptr);
	aac_fib_free(fibptr);
	return rcode;
}

static void aac_set_safw_target_qd(struct aac_dev *dev, int bus, int target)
{

	struct aac_ciss_identify_pd *identify_resp;

	if (dev->hba_map[bus][target].devtype != AAC_DEVTYPE_NATIVE_RAW)
		return;

	identify_resp = dev->hba_map[bus][target].safw_identify_resp;
	if (identify_resp == NULL) {
		dev->hba_map[bus][target].qd_limit = 32;
		return;
	}

	if (identify_resp->current_queue_depth_limit <= 0 ||
		identify_resp->current_queue_depth_limit > 255)
		dev->hba_map[bus][target].qd_limit = 32;
	else
		dev->hba_map[bus][target].qd_limit =
			identify_resp->current_queue_depth_limit;
}

static int aac_issue_safw_bmic_identify(struct aac_dev *dev,
	struct aac_ciss_identify_pd **identify_resp, u32 bus, u32 target)
{
	int rcode = -ENOMEM;
	int datasize;
	struct aac_srb_unit srbu;
	struct aac_srb *srbcmd;
	struct aac_ciss_identify_pd *identify_reply;

	datasize = sizeof(struct aac_ciss_identify_pd);
	identify_reply = kmalloc(datasize, GFP_KERNEL);
	if (!identify_reply)
		goto out;

	memset(&srbu, 0, sizeof(struct aac_srb_unit));

	srbcmd = &srbu.srb;
	srbcmd->flags	= cpu_to_le32(SRB_DataIn);
	srbcmd->cdb[0]	= 0x26;
	srbcmd->cdb[2]	= (u8)((AAC_MAX_LUN + target) & 0x00FF);
	srbcmd->cdb[6]	= CISS_IDENTIFY_PHYSICAL_DEVICE;

	rcode = aac_send_safw_bmic_cmd(dev, &srbu, identify_reply, datasize);
	if (unlikely(rcode < 0))
		goto mem_free_all;

	*identify_resp = identify_reply;

out:
	return rcode;
mem_free_all:
	kfree(identify_reply);
	goto out;
}

static inline void aac_free_safw_ciss_luns(struct aac_dev *dev)
{
	kfree(dev->safw_phys_luns);
	dev->safw_phys_luns = NULL;
}

/**
 *	aac_get_safw_ciss_luns()	Process topology change
 *	@dev:		aac_dev structure
 *
 *	Execute a CISS REPORT PHYS LUNS and process the results into
 *	the current hba_map.
 */
static int aac_get_safw_ciss_luns(struct aac_dev *dev)
{
	int rcode = -ENOMEM;
	int datasize;
	struct aac_srb *srbcmd;
	struct aac_srb_unit srbu;
	struct aac_ciss_phys_luns_resp *phys_luns;

	datasize = sizeof(struct aac_ciss_phys_luns_resp) +
		(AAC_MAX_TARGETS - 1) * sizeof(struct _ciss_lun);
	phys_luns = kmalloc(datasize, GFP_KERNEL);
	if (phys_luns == NULL)
		goto out;

	memset(&srbu, 0, sizeof(struct aac_srb_unit));

	srbcmd = &srbu.srb;
	srbcmd->flags	= cpu_to_le32(SRB_DataIn);
	srbcmd->cdb[0]	= CISS_REPORT_PHYSICAL_LUNS;
	srbcmd->cdb[1]	= 2; /* extended reporting */
	srbcmd->cdb[8]	= (u8)(datasize >> 8);
	srbcmd->cdb[9]	= (u8)(datasize);

	rcode = aac_send_safw_bmic_cmd(dev, &srbu, phys_luns, datasize);
	if (unlikely(rcode < 0))
		goto mem_free_all;

	if (phys_luns->resp_flag != 2) {
		rcode = -ENOMSG;
		goto mem_free_all;
	}

	dev->safw_phys_luns = phys_luns;

out:
	return rcode;
mem_free_all:
	kfree(phys_luns);
	goto out;
}

static inline u32 aac_get_safw_phys_lun_count(struct aac_dev *dev)
{
	return get_unaligned_be32(&dev->safw_phys_luns->list_length[0])/24;
}

static inline u32 aac_get_safw_phys_bus(struct aac_dev *dev, int lun)
{
	return dev->safw_phys_luns->lun[lun].level2[1] & 0x3f;
}

static inline u32 aac_get_safw_phys_target(struct aac_dev *dev, int lun)
{
	return dev->safw_phys_luns->lun[lun].level2[0];
}

static inline u32 aac_get_safw_phys_expose_flag(struct aac_dev *dev, int lun)
{
	return dev->safw_phys_luns->lun[lun].bus >> 6;
}

static inline u32 aac_get_safw_phys_attribs(struct aac_dev *dev, int lun)
{
	return dev->safw_phys_luns->lun[lun].node_ident[9];
}

static inline u32 aac_get_safw_phys_nexus(struct aac_dev *dev, int lun)
{
	return *((u32 *)&dev->safw_phys_luns->lun[lun].node_ident[12]);
}

static inline u32 aac_get_safw_phys_device_type(struct aac_dev *dev, int lun)
{
	return dev->safw_phys_luns->lun[lun].node_ident[8];
}

static inline void aac_free_safw_identify_resp(struct aac_dev *dev,
						int bus, int target)
{
	kfree(dev->hba_map[bus][target].safw_identify_resp);
	dev->hba_map[bus][target].safw_identify_resp = NULL;
}

static inline void aac_free_safw_all_identify_resp(struct aac_dev *dev,
	int lun_count)
{
	int luns;
	int i;
	u32 bus;
	u32 target;

	luns = aac_get_safw_phys_lun_count(dev);

	if (luns < lun_count)
		lun_count = luns;
	else if (lun_count < 0)
		lun_count = luns;

	for (i = 0; i < lun_count; i++) {
		bus = aac_get_safw_phys_bus(dev, i);
		target = aac_get_safw_phys_target(dev, i);

		aac_free_safw_identify_resp(dev, bus, target);
	}
}

static int aac_get_safw_attr_all_targets(struct aac_dev *dev)
{
	int i;
	int rcode = 0;
	u32 lun_count;
	u32 bus;
	u32 target;
	struct aac_ciss_identify_pd *identify_resp = NULL;

	lun_count = aac_get_safw_phys_lun_count(dev);

	for (i = 0; i < lun_count; ++i) {

		bus = aac_get_safw_phys_bus(dev, i);
		target = aac_get_safw_phys_target(dev, i);

		rcode = aac_issue_safw_bmic_identify(dev,
						&identify_resp, bus, target);

		if (unlikely(rcode < 0))
			goto free_identify_resp;

		dev->hba_map[bus][target].safw_identify_resp = identify_resp;
	}

out:
	return rcode;
free_identify_resp:
	aac_free_safw_all_identify_resp(dev, i);
	goto out;
}

/**
 *	aac_set_safw_attr_all_targets-	update current hba map with data from FW
 *	@dev:	aac_dev structure
 *	@phys_luns: FW information from report phys luns
 *	@rescan: Indicates scan type
 *
 *	Update our hba map with the information gathered from the FW
 */
static void aac_set_safw_attr_all_targets(struct aac_dev *dev)
{
	/* ok and extended reporting */
	u32 lun_count, nexus;
	u32 i, bus, target;
	u8 expose_flag, attribs;

	lun_count = aac_get_safw_phys_lun_count(dev);

	dev->scan_counter++;

	for (i = 0; i < lun_count; ++i) {

		bus = aac_get_safw_phys_bus(dev, i);
		target = aac_get_safw_phys_target(dev, i);
		expose_flag = aac_get_safw_phys_expose_flag(dev, i);
		attribs = aac_get_safw_phys_attribs(dev, i);
		nexus = aac_get_safw_phys_nexus(dev, i);

		if (bus >= AAC_MAX_BUSES || target >= AAC_MAX_TARGETS)
			continue;

		if (expose_flag != 0) {
			dev->hba_map[bus][target].devtype =
				AAC_DEVTYPE_RAID_MEMBER;
			continue;
		}

		if (nexus != 0 && (attribs & 8)) {
			dev->hba_map[bus][target].devtype =
				AAC_DEVTYPE_NATIVE_RAW;
			dev->hba_map[bus][target].rmw_nexus =
					nexus;
		} else
			dev->hba_map[bus][target].devtype =
				AAC_DEVTYPE_ARC_RAW;

		dev->hba_map[bus][target].scan_counter = dev->scan_counter;

		aac_set_safw_target_qd(dev, bus, target);
	}
}

static int aac_setup_safw_targets(struct aac_dev *dev)
{
	int rcode = 0;

	rcode = aac_get_containers(dev);
	if (unlikely(rcode < 0))
		goto out;

	rcode = aac_get_safw_ciss_luns(dev);
	if (unlikely(rcode < 0))
		goto out;

	rcode = aac_get_safw_attr_all_targets(dev);
	if (unlikely(rcode < 0))
		goto free_ciss_luns;

	aac_set_safw_attr_all_targets(dev);

	aac_free_safw_all_identify_resp(dev, -1);
free_ciss_luns:
	aac_free_safw_ciss_luns(dev);
out:
	return rcode;
}

int aac_setup_safw_adapter(struct aac_dev *dev)
{
	return aac_setup_safw_targets(dev);
}

int aac_get_adapter_info(struct aac_dev* dev)
{
	struct fib* fibptr;
	int rcode;
	u32 tmp, bus, target;
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
		/* FIB should be freed only after
		 * getting the response from the F/W */
		if (rcode != -ERESTARTSYS) {
			aac_fib_complete(fibptr);
			aac_fib_free(fibptr);
		}
		return rcode;
	}
	memcpy(&dev->adapter_info, info, sizeof(*info));

	dev->supplement_adapter_info.virt_device_bus = 0xffff;
	if (dev->adapter_info.options & AAC_OPT_SUPPLEMENT_ADAPTER_INFO) {
		struct aac_supplement_adapter_info * sinfo;

		aac_fib_init(fibptr);

		sinfo = (struct aac_supplement_adapter_info *) fib_data(fibptr);

		memset(sinfo,0,sizeof(*sinfo));

		rcode = aac_fib_send(RequestSupplementAdapterInfo,
				 fibptr,
				 sizeof(*sinfo),
				 FsaNormal,
				 1, 1,
				 NULL,
				 NULL);

		if (rcode >= 0)
			memcpy(&dev->supplement_adapter_info, sinfo, sizeof(*sinfo));
		if (rcode == -ERESTARTSYS) {
			fibptr = aac_fib_alloc(dev);
			if (!fibptr)
				return -ENOMEM;
		}

	}

	/* reset all previous mapped devices (i.e. for init. after IOP_RESET) */
	for (bus = 0; bus < AAC_MAX_BUSES; bus++) {
		for (target = 0; target < AAC_MAX_TARGETS; target++) {
			dev->hba_map[bus][target].devtype = 0;
			dev->hba_map[bus][target].qd_limit = 0;
		}
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

	/* reasoned default */
	dev->maximum_num_physicals = 16;
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
			(int)sizeof(dev->supplement_adapter_info.build_date),
			dev->supplement_adapter_info.build_date);
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
		if (aac_get_serial_number(
		  shost_to_class(dev->scsi_host_ptr), buffer))
			printk(KERN_INFO "%s%d: serial %s",
			  dev->name, dev->id, buffer);
		if (dev->supplement_adapter_info.vpd_info.tsid[0]) {
			printk(KERN_INFO "%s%d: TSID %.*s\n",
			  dev->name, dev->id,
			  (int)sizeof(dev->supplement_adapter_info
							.vpd_info.tsid),
				dev->supplement_adapter_info.vpd_info.tsid);
		}
		if (!aac_check_reset || ((aac_check_reset == 1) &&
		  (dev->supplement_adapter_info.supported_options2 &
		  AAC_OPTION_IGNORE_RESET))) {
			printk(KERN_INFO "%s%d: Reset Adapter Ignored\n",
			  dev->name, dev->id);
		}
	}

	dev->cache_protected = 0;
	dev->jbod = ((dev->supplement_adapter_info.feature_bits &
		AAC_FEATURE_JBOD) != 0);
	dev->nondasd_support = 0;
	dev->raid_scsi_mode = 0;
	if(dev->adapter_info.options & AAC_OPT_NONDASD)
		dev->nondasd_support = 1;

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

	if (nondasd != -1)
		dev->nondasd_support = (nondasd!=0);
	if (dev->nondasd_support && !dev->in_reset)
		printk(KERN_INFO "%s%d: Non-DASD support enabled.\n",dev->name, dev->id);

	if (dma_get_required_mask(&dev->pdev->dev) > DMA_BIT_MASK(32))
		dev->needs_dac = 1;
	dev->dac_support = 0;
	if ((sizeof(dma_addr_t) > 4) && dev->needs_dac &&
	    (dev->adapter_info.options & AAC_OPT_SGMAP_HOST64)) {
		if (!dev->in_reset)
			printk(KERN_INFO "%s%d: 64bit support enabled.\n",
				dev->name, dev->id);
		dev->dac_support = 1;
	}

	if(dacmode != -1) {
		dev->dac_support = (dacmode!=0);
	}

	/* avoid problems with AAC_QUIRK_SCSI_32 controllers */
	if (dev->dac_support &&	(aac_get_driver_ident(dev->cardtype)->quirks
		& AAC_QUIRK_SCSI_32)) {
		dev->nondasd_support = 0;
		dev->jbod = 0;
		expose_physicals = 0;
	}

	if (dev->dac_support) {
		if (!pci_set_dma_mask(dev->pdev, DMA_BIT_MASK(64))) {
			if (!dev->in_reset)
				dev_info(&dev->pdev->dev, "64 Bit DAC enabled\n");
		} else if (!pci_set_dma_mask(dev->pdev, DMA_BIT_MASK(32))) {
			dev_info(&dev->pdev->dev, "DMA mask set failed, 64 Bit DAC disabled\n");
			dev->dac_support = 0;
		} else {
			dev_info(&dev->pdev->dev, "No suitable DMA available\n");
			rcode = -ENOMEM;
		}
	}
	/*
	 * Deal with configuring for the individualized limits of each packet
	 * interface.
	 */
	dev->a_ops.adapter_scsi = (dev->dac_support)
	  ? ((aac_get_driver_ident(dev->cardtype)->quirks & AAC_QUIRK_SCSI_32)
				? aac_scsi_32_64
				: aac_scsi_64)
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
		if (!(dev->adapter_info.options & AAC_OPT_NEW_COMM)) {
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
	if (!dev->sync_mode && dev->sa_firmware &&
		dev->scsi_host_ptr->sg_tablesize > HBA_MAX_SG_SEPARATE)
		dev->scsi_host_ptr->sg_tablesize = dev->sg_tablesize =
			HBA_MAX_SG_SEPARATE;

	/* FIB should be freed only after getting the response from the F/W */
	if (rcode != -ERESTARTSYS) {
		aac_fib_complete(fibptr);
		aac_fib_free(fibptr);
	}

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
	switch (le32_to_cpu(readreply->status)) {
	case ST_OK:
		scsicmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8 |
			SAM_STAT_GOOD;
		dev->fsa_dev[cid].sense_data.sense_key = NO_SENSE;
		break;
	case ST_NOT_READY:
		scsicmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8 |
			SAM_STAT_CHECK_CONDITION;
		set_sense(&dev->fsa_dev[cid].sense_data, NOT_READY,
		  SENCODE_BECOMING_READY, ASENCODE_BECOMING_READY, 0, 0);
		memcpy(scsicmd->sense_buffer, &dev->fsa_dev[cid].sense_data,
		       min_t(size_t, sizeof(dev->fsa_dev[cid].sense_data),
			     SCSI_SENSE_BUFFERSIZE));
		break;
	case ST_MEDERR:
		scsicmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8 |
			SAM_STAT_CHECK_CONDITION;
		set_sense(&dev->fsa_dev[cid].sense_data, MEDIUM_ERROR,
		  SENCODE_UNRECOVERED_READ_ERROR, ASENCODE_NO_SENSE, 0, 0);
		memcpy(scsicmd->sense_buffer, &dev->fsa_dev[cid].sense_data,
		       min_t(size_t, sizeof(dev->fsa_dev[cid].sense_data),
			     SCSI_SENSE_BUFFERSIZE));
		break;
	default:
#ifdef AAC_DETAILED_STATUS_INFO
		printk(KERN_WARNING "io_callback: io failed, status = %d\n",
		  le32_to_cpu(readreply->status));
#endif
		scsicmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8 |
			SAM_STAT_CHECK_CONDITION;
		set_sense(&dev->fsa_dev[cid].sense_data,
		  HARDWARE_ERROR, SENCODE_INTERNAL_TARGET_FAILURE,
		  ASENCODE_INTERNAL_TARGET_FAILURE, 0, 0);
		memcpy(scsicmd->sense_buffer, &dev->fsa_dev[cid].sense_data,
		       min_t(size_t, sizeof(dev->fsa_dev[cid].sense_data),
			     SCSI_SENSE_BUFFERSIZE));
		break;
	}
	aac_fib_complete(fibptr);

	scsicmd->scsi_done(scsicmd);
}

static int aac_read(struct scsi_cmnd * scsicmd)
{
	u64 lba;
	u32 count;
	int status;
	struct aac_dev *dev;
	struct fib * cmd_fibcontext;
	int cid;

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

		lba =	((u64)scsicmd->cmnd[2] << 56) |
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

	if ((lba + count) > (dev->fsa_dev[scmd_id(scsicmd)].size)) {
		cid = scmd_id(scsicmd);
		dprintk((KERN_DEBUG "aacraid: Illegal lba\n"));
		scsicmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8 |
			SAM_STAT_CHECK_CONDITION;
		set_sense(&dev->fsa_dev[cid].sense_data,
			  ILLEGAL_REQUEST, SENCODE_LBA_OUT_OF_RANGE,
			  ASENCODE_INTERNAL_TARGET_FAILURE, 0, 0);
		memcpy(scsicmd->sense_buffer, &dev->fsa_dev[cid].sense_data,
		       min_t(size_t, sizeof(dev->fsa_dev[cid].sense_data),
			     SCSI_SENSE_BUFFERSIZE));
		scsicmd->scsi_done(scsicmd);
		return 0;
	}

	dprintk((KERN_DEBUG "aac_read[cpu %d]: lba = %llu, t = %ld.\n",
	  smp_processor_id(), (unsigned long long)lba, jiffies));
	if (aac_adapter_bounds(dev,scsicmd,lba))
		return 0;
	/*
	 *	Alocate and initialize a Fib
	 */
	cmd_fibcontext = aac_fib_alloc_tag(dev, scsicmd);
	scsicmd->SCp.phase = AAC_OWNER_FIRMWARE;
	status = aac_adapter_read(cmd_fibcontext, scsicmd, lba, count);

	/*
	 *	Check that the command queued to the controller
	 */
	if (status == -EINPROGRESS)
		return 0;

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
	int cid;

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

		lba =	((u64)scsicmd->cmnd[2] << 56) |
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

	if ((lba + count) > (dev->fsa_dev[scmd_id(scsicmd)].size)) {
		cid = scmd_id(scsicmd);
		dprintk((KERN_DEBUG "aacraid: Illegal lba\n"));
		scsicmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8 |
			SAM_STAT_CHECK_CONDITION;
		set_sense(&dev->fsa_dev[cid].sense_data,
			  ILLEGAL_REQUEST, SENCODE_LBA_OUT_OF_RANGE,
			  ASENCODE_INTERNAL_TARGET_FAILURE, 0, 0);
		memcpy(scsicmd->sense_buffer, &dev->fsa_dev[cid].sense_data,
		       min_t(size_t, sizeof(dev->fsa_dev[cid].sense_data),
			     SCSI_SENSE_BUFFERSIZE));
		scsicmd->scsi_done(scsicmd);
		return 0;
	}

	dprintk((KERN_DEBUG "aac_write[cpu %d]: lba = %llu, t = %ld.\n",
	  smp_processor_id(), (unsigned long long)lba, jiffies));
	if (aac_adapter_bounds(dev,scsicmd,lba))
		return 0;
	/*
	 *	Allocate and initialize a Fib then setup a BlockWrite command
	 */
	cmd_fibcontext = aac_fib_alloc_tag(dev, scsicmd);
	scsicmd->SCp.phase = AAC_OWNER_FIRMWARE;
	status = aac_adapter_write(cmd_fibcontext, scsicmd, lba, count, fua);

	/*
	 *	Check that the command queued to the controller
	 */
	if (status == -EINPROGRESS)
		return 0;

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
		set_sense(&dev->fsa_dev[cid].sense_data,
		  HARDWARE_ERROR, SENCODE_INTERNAL_TARGET_FAILURE,
		  ASENCODE_INTERNAL_TARGET_FAILURE, 0, 0);
		memcpy(cmd->sense_buffer, &dev->fsa_dev[cid].sense_data,
		       min_t(size_t, sizeof(dev->fsa_dev[cid].sense_data),
			     SCSI_SENSE_BUFFERSIZE));
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
	u64 lba = ((u64)scsicmd->cmnd[2] << 24) | (scsicmd->cmnd[3] << 16) |
		(scsicmd->cmnd[4] << 8) | scsicmd->cmnd[5];
	u32 count = (scsicmd->cmnd[7] << 8) | scsicmd->cmnd[8];
	unsigned long flags;

	/*
	 * Wait for all outstanding queued commands to complete to this
	 * specific target (block).
	 */
	spin_lock_irqsave(&sdev->list_lock, flags);
	list_for_each_entry(cmd, &sdev->cmd_list, list)
		if (cmd->SCp.phase == AAC_OWNER_FIRMWARE) {
			u64 cmnd_lba;
			u32 cmnd_count;

			if (cmd->cmnd[0] == WRITE_6) {
				cmnd_lba = ((cmd->cmnd[1] & 0x1F) << 16) |
					(cmd->cmnd[2] << 8) |
					cmd->cmnd[3];
				cmnd_count = cmd->cmnd[4];
				if (cmnd_count == 0)
					cmnd_count = 256;
			} else if (cmd->cmnd[0] == WRITE_16) {
				cmnd_lba = ((u64)cmd->cmnd[2] << 56) |
					((u64)cmd->cmnd[3] << 48) |
					((u64)cmd->cmnd[4] << 40) |
					((u64)cmd->cmnd[5] << 32) |
					((u64)cmd->cmnd[6] << 24) |
					(cmd->cmnd[7] << 16) |
					(cmd->cmnd[8] << 8) |
					cmd->cmnd[9];
				cmnd_count = (cmd->cmnd[10] << 24) |
					(cmd->cmnd[11] << 16) |
					(cmd->cmnd[12] << 8) |
					cmd->cmnd[13];
			} else if (cmd->cmnd[0] == WRITE_12) {
				cmnd_lba = ((u64)cmd->cmnd[2] << 24) |
					(cmd->cmnd[3] << 16) |
					(cmd->cmnd[4] << 8) |
					cmd->cmnd[5];
				cmnd_count = (cmd->cmnd[6] << 24) |
					(cmd->cmnd[7] << 16) |
					(cmd->cmnd[8] << 8) |
					cmd->cmnd[9];
			} else if (cmd->cmnd[0] == WRITE_10) {
				cmnd_lba = ((u64)cmd->cmnd[2] << 24) |
					(cmd->cmnd[3] << 16) |
					(cmd->cmnd[4] << 8) |
					cmd->cmnd[5];
				cmnd_count = (cmd->cmnd[7] << 8) |
					cmd->cmnd[8];
			} else
				continue;
			if (((cmnd_lba + cmnd_count) < lba) ||
			  (count && ((lba + count) < cmnd_lba)))
				continue;
			++active;
			break;
		}

	spin_unlock_irqrestore(&sdev->list_lock, flags);

	/*
	 *	Yield the processor (requeue for later)
	 */
	if (active)
		return SCSI_MLQUEUE_DEVICE_BUSY;

	aac = (struct aac_dev *)sdev->host->hostdata;
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
	scsicmd->SCp.phase = AAC_OWNER_FIRMWARE;

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
	if (status == -EINPROGRESS)
		return 0;

	printk(KERN_WARNING
		"aac_synchronize: aac_fib_send failed with status: %d.\n", status);
	aac_fib_complete(cmd_fibcontext);
	aac_fib_free(cmd_fibcontext);
	return SCSI_MLQUEUE_HOST_BUSY;
}

static void aac_start_stop_callback(void *context, struct fib *fibptr)
{
	struct scsi_cmnd *scsicmd = context;

	if (!aac_valid_context(scsicmd, fibptr))
		return;

	BUG_ON(fibptr == NULL);

	scsicmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8 | SAM_STAT_GOOD;

	aac_fib_complete(fibptr);
	aac_fib_free(fibptr);
	scsicmd->scsi_done(scsicmd);
}

static int aac_start_stop(struct scsi_cmnd *scsicmd)
{
	int status;
	struct fib *cmd_fibcontext;
	struct aac_power_management *pmcmd;
	struct scsi_device *sdev = scsicmd->device;
	struct aac_dev *aac = (struct aac_dev *)sdev->host->hostdata;

	if (!(aac->supplement_adapter_info.supported_options2 &
	      AAC_OPTION_POWER_MANAGEMENT)) {
		scsicmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8 |
				  SAM_STAT_GOOD;
		scsicmd->scsi_done(scsicmd);
		return 0;
	}

	if (aac->in_reset)
		return SCSI_MLQUEUE_HOST_BUSY;

	/*
	 *	Allocate and initialize a Fib
	 */
	cmd_fibcontext = aac_fib_alloc_tag(aac, scsicmd);

	aac_fib_init(cmd_fibcontext);

	pmcmd = fib_data(cmd_fibcontext);
	pmcmd->command = cpu_to_le32(VM_ContainerConfig);
	pmcmd->type = cpu_to_le32(CT_POWER_MANAGEMENT);
	/* Eject bit ignored, not relevant */
	pmcmd->sub = (scsicmd->cmnd[4] & 1) ?
		cpu_to_le32(CT_PM_START_UNIT) : cpu_to_le32(CT_PM_STOP_UNIT);
	pmcmd->cid = cpu_to_le32(sdev_id(sdev));
	pmcmd->parm = (scsicmd->cmnd[1] & 1) ?
		cpu_to_le32(CT_PM_UNIT_IMMEDIATE) : 0;
	scsicmd->SCp.phase = AAC_OWNER_FIRMWARE;

	/*
	 *	Now send the Fib to the adapter
	 */
	status = aac_fib_send(ContainerCommand,
		  cmd_fibcontext,
		  sizeof(struct aac_power_management),
		  FsaNormal,
		  0, 1,
		  (fib_callback)aac_start_stop_callback,
		  (void *)scsicmd);

	/*
	 *	Check that the command queued to the controller
	 */
	if (status == -EINPROGRESS)
		return 0;

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
	u32 cid, bus;
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
				goto scsi_done_ret;
			}

			/*
			 *	If the target container doesn't exist, it may have
			 *	been newly created
			 */
			if (((fsa_dev_ptr[cid].valid & 1) == 0) ||
			  (fsa_dev_ptr[cid].sense_data.sense_key ==
			   NOT_READY)) {
				switch (scsicmd->cmnd[0]) {
				case SERVICE_ACTION_IN_16:
					if (!(dev->raw_io_interface) ||
					    !(dev->raw_io_64) ||
					    ((scsicmd->cmnd[1] & 0x1f) != SAI_READ_CAPACITY_16))
						break;
					/* fall through */
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
			bus = aac_logical_to_phys(scmd_channel(scsicmd));

			if (bus < AAC_MAX_BUSES && cid < AAC_MAX_TARGETS &&
				dev->hba_map[bus][cid].devtype
					== AAC_DEVTYPE_NATIVE_RAW) {
				if (dev->in_reset)
					return -1;
				return aac_send_hba_fib(scsicmd);
			} else if (dev->nondasd_support || expose_physicals ||
				dev->jbod) {
				if (dev->in_reset)
					return -1;
				return aac_send_srb_fib(scsicmd);
			} else {
				scsicmd->result = DID_NO_CONNECT << 16;
				goto scsi_done_ret;
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
		set_sense(&dev->fsa_dev[cid].sense_data,
		  ILLEGAL_REQUEST, SENCODE_INVALID_COMMAND,
		  ASENCODE_INVALID_COMMAND, 0, 0);
		memcpy(scsicmd->sense_buffer, &dev->fsa_dev[cid].sense_data,
		       min_t(size_t, sizeof(dev->fsa_dev[cid].sense_data),
			     SCSI_SENSE_BUFFERSIZE));
		goto scsi_done_ret;
	}

	switch (scsicmd->cmnd[0]) {
	case READ_6:
	case READ_10:
	case READ_12:
	case READ_16:
		if (dev->in_reset)
			return -1;
		return aac_read(scsicmd);

	case WRITE_6:
	case WRITE_10:
	case WRITE_12:
	case WRITE_16:
		if (dev->in_reset)
			return -1;
		return aac_write(scsicmd);

	case SYNCHRONIZE_CACHE:
		if (((aac_cache & 6) == 6) && dev->cache_protected) {
			scsicmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8 |
					  SAM_STAT_GOOD;
			break;
		}
		/* Issue FIB to tell Firmware to flush it's cache */
		if ((aac_cache & 6) != 2)
			return aac_synchronize(scsicmd);
		/* fall through */
	case INQUIRY:
	{
		struct inquiry_data inq_data;

		dprintk((KERN_DEBUG "INQUIRY command, ID: %d.\n", cid));
		memset(&inq_data, 0, sizeof (struct inquiry_data));

		if ((scsicmd->cmnd[1] & 0x1) && aac_wwn) {
			char *arr = (char *)&inq_data;

			/* EVPD bit set */
			arr[0] = (scmd_id(scsicmd) == host->this_id) ?
			  INQD_PDT_PROC : INQD_PDT_DA;
			if (scsicmd->cmnd[2] == 0) {
				/* supported vital product data pages */
				arr[3] = 3;
				arr[4] = 0x0;
				arr[5] = 0x80;
				arr[6] = 0x83;
				arr[1] = scsicmd->cmnd[2];
				scsi_sg_copy_from_buffer(scsicmd, &inq_data,
							 sizeof(inq_data));
				scsicmd->result = DID_OK << 16 |
						  COMMAND_COMPLETE << 8 |
						  SAM_STAT_GOOD;
			} else if (scsicmd->cmnd[2] == 0x80) {
				/* unit serial number page */
				arr[3] = setinqserial(dev, &arr[4],
				  scmd_id(scsicmd));
				arr[1] = scsicmd->cmnd[2];
				scsi_sg_copy_from_buffer(scsicmd, &inq_data,
							 sizeof(inq_data));
				if (aac_wwn != 2)
					return aac_get_container_serial(
						scsicmd);
				scsicmd->result = DID_OK << 16 |
						  COMMAND_COMPLETE << 8 |
						  SAM_STAT_GOOD;
			} else if (scsicmd->cmnd[2] == 0x83) {
				/* vpd page 0x83 - Device Identification Page */
				char *sno = (char *)&inq_data;
				sno[3] = setinqserial(dev, &sno[4],
						      scmd_id(scsicmd));
				if (aac_wwn != 2)
					return aac_get_container_serial(
						scsicmd);
				scsicmd->result = DID_OK << 16 |
						  COMMAND_COMPLETE << 8 |
						  SAM_STAT_GOOD;
			} else {
				/* vpd page not implemented */
				scsicmd->result = DID_OK << 16 |
				  COMMAND_COMPLETE << 8 |
				  SAM_STAT_CHECK_CONDITION;
				set_sense(&dev->fsa_dev[cid].sense_data,
				  ILLEGAL_REQUEST, SENCODE_INVALID_CDB_FIELD,
				  ASENCODE_NO_SENSE, 7, 2);
				memcpy(scsicmd->sense_buffer,
				  &dev->fsa_dev[cid].sense_data,
				  min_t(size_t,
					sizeof(dev->fsa_dev[cid].sense_data),
					SCSI_SENSE_BUFFERSIZE));
			}
			break;
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
			scsi_sg_copy_from_buffer(scsicmd, &inq_data,
						 sizeof(inq_data));
			scsicmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8 |
					  SAM_STAT_GOOD;
			break;
		}
		if (dev->in_reset)
			return -1;
		setinqstr(dev, (void *) (inq_data.inqd_vid), fsa_dev_ptr[cid].type);
		inq_data.inqd_pdt = INQD_PDT_DA;	/* Direct/random access device */
		scsi_sg_copy_from_buffer(scsicmd, &inq_data, sizeof(inq_data));
		return aac_get_container_name(scsicmd);
	}
	case SERVICE_ACTION_IN_16:
		if (!(dev->raw_io_interface) ||
		    !(dev->raw_io_64) ||
		    ((scsicmd->cmnd[1] & 0x1f) != SAI_READ_CAPACITY_16))
			break;
	{
		u64 capacity;
		char cp[13];
		unsigned int alloc_len;

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
		cp[8] = (fsa_dev_ptr[cid].block_size >> 24) & 0xff;
		cp[9] = (fsa_dev_ptr[cid].block_size >> 16) & 0xff;
		cp[10] = (fsa_dev_ptr[cid].block_size >> 8) & 0xff;
		cp[11] = (fsa_dev_ptr[cid].block_size) & 0xff;
		cp[12] = 0;

		alloc_len = ((scsicmd->cmnd[10] << 24)
			     + (scsicmd->cmnd[11] << 16)
			     + (scsicmd->cmnd[12] << 8) + scsicmd->cmnd[13]);

		alloc_len = min_t(size_t, alloc_len, sizeof(cp));
		scsi_sg_copy_from_buffer(scsicmd, cp, alloc_len);
		if (alloc_len < scsi_bufflen(scsicmd))
			scsi_set_resid(scsicmd,
				       scsi_bufflen(scsicmd) - alloc_len);

		/* Do not cache partition table for arrays */
		scsicmd->device->removable = 1;

		scsicmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8 |
				  SAM_STAT_GOOD;
		break;
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
		cp[4] = (fsa_dev_ptr[cid].block_size >> 24) & 0xff;
		cp[5] = (fsa_dev_ptr[cid].block_size >> 16) & 0xff;
		cp[6] = (fsa_dev_ptr[cid].block_size >> 8) & 0xff;
		cp[7] = (fsa_dev_ptr[cid].block_size) & 0xff;
		scsi_sg_copy_from_buffer(scsicmd, cp, sizeof(cp));
		/* Do not cache partition table for arrays */
		scsicmd->device->removable = 1;
		scsicmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8 |
				  SAM_STAT_GOOD;
		break;
	}

	case MODE_SENSE:
	{
		int mode_buf_length = 4;
		u32 capacity;
		aac_modep_data mpd;

		if (fsa_dev_ptr[cid].size <= 0x100000000ULL)
			capacity = fsa_dev_ptr[cid].size - 1;
		else
			capacity = (u32)-1;

		dprintk((KERN_DEBUG "MODE SENSE command.\n"));
		memset((char *)&mpd, 0, sizeof(aac_modep_data));

		/* Mode data length */
		mpd.hd.data_length = sizeof(mpd.hd) - 1;
		/* Medium type - default */
		mpd.hd.med_type = 0;
		/* Device-specific param,
		   bit 8: 0/1 = write enabled/protected
		   bit 4: 0/1 = FUA enabled */
		mpd.hd.dev_par = 0;

		if (dev->raw_io_interface && ((aac_cache & 5) != 1))
			mpd.hd.dev_par = 0x10;
		if (scsicmd->cmnd[1] & 0x8)
			mpd.hd.bd_length = 0;	/* Block descriptor length */
		else {
			mpd.hd.bd_length = sizeof(mpd.bd);
			mpd.hd.data_length += mpd.hd.bd_length;
			mpd.bd.block_length[0] =
				(fsa_dev_ptr[cid].block_size >> 16) & 0xff;
			mpd.bd.block_length[1] =
				(fsa_dev_ptr[cid].block_size >> 8) &  0xff;
			mpd.bd.block_length[2] =
				fsa_dev_ptr[cid].block_size  & 0xff;

			mpd.mpc_buf[0] = scsicmd->cmnd[2];
			if (scsicmd->cmnd[2] == 0x1C) {
				/* page length */
				mpd.mpc_buf[1] = 0xa;
				/* Mode data length */
				mpd.hd.data_length = 23;
			} else {
				/* Mode data length */
				mpd.hd.data_length = 15;
			}

			if (capacity > 0xffffff) {
				mpd.bd.block_count[0] = 0xff;
				mpd.bd.block_count[1] = 0xff;
				mpd.bd.block_count[2] = 0xff;
			} else {
				mpd.bd.block_count[0] = (capacity >> 16) & 0xff;
				mpd.bd.block_count[1] = (capacity >> 8) & 0xff;
				mpd.bd.block_count[2] = capacity  & 0xff;
			}
		}
		if (((scsicmd->cmnd[2] & 0x3f) == 8) ||
		  ((scsicmd->cmnd[2] & 0x3f) == 0x3f)) {
			mpd.hd.data_length += 3;
			mpd.mpc_buf[0] = 8;
			mpd.mpc_buf[1] = 1;
			mpd.mpc_buf[2] = ((aac_cache & 6) == 2)
				? 0 : 0x04; /* WCE */
			mode_buf_length = sizeof(mpd);
		}

		if (mode_buf_length > scsicmd->cmnd[4])
			mode_buf_length = scsicmd->cmnd[4];
		else
			mode_buf_length = sizeof(mpd);
		scsi_sg_copy_from_buffer(scsicmd,
					 (char *)&mpd,
					 mode_buf_length);
		scsicmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8 |
				  SAM_STAT_GOOD;
		break;
	}
	case MODE_SENSE_10:
	{
		u32 capacity;
		int mode_buf_length = 8;
		aac_modep10_data mpd10;

		if (fsa_dev_ptr[cid].size <= 0x100000000ULL)
			capacity = fsa_dev_ptr[cid].size - 1;
		else
			capacity = (u32)-1;

		dprintk((KERN_DEBUG "MODE SENSE 10 byte command.\n"));
		memset((char *)&mpd10, 0, sizeof(aac_modep10_data));
		/* Mode data length (MSB) */
		mpd10.hd.data_length[0] = 0;
		/* Mode data length (LSB) */
		mpd10.hd.data_length[1] = sizeof(mpd10.hd) - 1;
		/* Medium type - default */
		mpd10.hd.med_type = 0;
		/* Device-specific param,
		   bit 8: 0/1 = write enabled/protected
		   bit 4: 0/1 = FUA enabled */
		mpd10.hd.dev_par = 0;

		if (dev->raw_io_interface && ((aac_cache & 5) != 1))
			mpd10.hd.dev_par = 0x10;
		mpd10.hd.rsrvd[0] = 0;	/* reserved */
		mpd10.hd.rsrvd[1] = 0;	/* reserved */
		if (scsicmd->cmnd[1] & 0x8) {
			/* Block descriptor length (MSB) */
			mpd10.hd.bd_length[0] = 0;
			/* Block descriptor length (LSB) */
			mpd10.hd.bd_length[1] = 0;
		} else {
			mpd10.hd.bd_length[0] = 0;
			mpd10.hd.bd_length[1] = sizeof(mpd10.bd);

			mpd10.hd.data_length[1] += mpd10.hd.bd_length[1];

			mpd10.bd.block_length[0] =
				(fsa_dev_ptr[cid].block_size >> 16) & 0xff;
			mpd10.bd.block_length[1] =
				(fsa_dev_ptr[cid].block_size >> 8) & 0xff;
			mpd10.bd.block_length[2] =
				fsa_dev_ptr[cid].block_size  & 0xff;

			if (capacity > 0xffffff) {
				mpd10.bd.block_count[0] = 0xff;
				mpd10.bd.block_count[1] = 0xff;
				mpd10.bd.block_count[2] = 0xff;
			} else {
				mpd10.bd.block_count[0] =
					(capacity >> 16) & 0xff;
				mpd10.bd.block_count[1] =
					(capacity >> 8) & 0xff;
				mpd10.bd.block_count[2] =
					capacity  & 0xff;
			}
		}
		if (((scsicmd->cmnd[2] & 0x3f) == 8) ||
		  ((scsicmd->cmnd[2] & 0x3f) == 0x3f)) {
			mpd10.hd.data_length[1] += 3;
			mpd10.mpc_buf[0] = 8;
			mpd10.mpc_buf[1] = 1;
			mpd10.mpc_buf[2] = ((aac_cache & 6) == 2)
				? 0 : 0x04; /* WCE */
			mode_buf_length = sizeof(mpd10);
			if (mode_buf_length > scsicmd->cmnd[8])
				mode_buf_length = scsicmd->cmnd[8];
		}
		scsi_sg_copy_from_buffer(scsicmd,
					 (char *)&mpd10,
					 mode_buf_length);

		scsicmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8 |
				  SAM_STAT_GOOD;
		break;
	}
	case REQUEST_SENSE:
		dprintk((KERN_DEBUG "REQUEST SENSE command.\n"));
		memcpy(scsicmd->sense_buffer, &dev->fsa_dev[cid].sense_data,
				sizeof(struct sense_data));
		memset(&dev->fsa_dev[cid].sense_data, 0,
				sizeof(struct sense_data));
		scsicmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8 |
				  SAM_STAT_GOOD;
		break;

	case ALLOW_MEDIUM_REMOVAL:
		dprintk((KERN_DEBUG "LOCK command.\n"));
		if (scsicmd->cmnd[4])
			fsa_dev_ptr[cid].locked = 1;
		else
			fsa_dev_ptr[cid].locked = 0;

		scsicmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8 |
				  SAM_STAT_GOOD;
		break;
	/*
	 *	These commands are all No-Ops
	 */
	case TEST_UNIT_READY:
		if (fsa_dev_ptr[cid].sense_data.sense_key == NOT_READY) {
			scsicmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8 |
				SAM_STAT_CHECK_CONDITION;
			set_sense(&dev->fsa_dev[cid].sense_data,
				  NOT_READY, SENCODE_BECOMING_READY,
				  ASENCODE_BECOMING_READY, 0, 0);
			memcpy(scsicmd->sense_buffer,
			       &dev->fsa_dev[cid].sense_data,
			       min_t(size_t,
				     sizeof(dev->fsa_dev[cid].sense_data),
				     SCSI_SENSE_BUFFERSIZE));
			break;
		}
		/* fall through */
	case RESERVE:
	case RELEASE:
	case REZERO_UNIT:
	case REASSIGN_BLOCKS:
	case SEEK_10:
		scsicmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8 |
				  SAM_STAT_GOOD;
		break;

	case START_STOP:
		return aac_start_stop(scsicmd);

	/* FALLTHRU */
	default:
	/*
	 *	Unhandled commands
	 */
		dprintk((KERN_WARNING "Unhandled SCSI Command: 0x%x.\n",
				scsicmd->cmnd[0]));
		scsicmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8 |
				SAM_STAT_CHECK_CONDITION;
		set_sense(&dev->fsa_dev[cid].sense_data,
			  ILLEGAL_REQUEST, SENCODE_INVALID_COMMAND,
			  ASENCODE_INVALID_COMMAND, 0, 0);
		memcpy(scsicmd->sense_buffer, &dev->fsa_dev[cid].sense_data,
				min_t(size_t,
				      sizeof(dev->fsa_dev[cid].sense_data),
				      SCSI_SENSE_BUFFERSIZE));
	}

scsi_done_ret:

	scsicmd->scsi_done(scsicmd);
	return 0;
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
	if (qd.cnum == -1) {
		if (qd.id < 0 || qd.id >= dev->maximum_num_containers)
			return -EINVAL;
		qd.cnum = qd.id;
	} else if ((qd.bus == -1) && (qd.id == -1) && (qd.lun == -1)) {
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

int aac_dev_ioctl(struct aac_dev *dev, unsigned int cmd, void __user *arg)
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
	struct aac_srb_reply *srbreply;
	struct scsi_cmnd *scsicmd;

	scsicmd = (struct scsi_cmnd *) context;

	if (!aac_valid_context(scsicmd, fibptr))
		return;

	BUG_ON(fibptr == NULL);

	srbreply = (struct aac_srb_reply *) fib_data(fibptr);

	scsicmd->sense_buffer[0] = '\0';  /* Initialize sense valid flag to false */

	if (fibptr->flags & FIB_CONTEXT_FLAG_FASTRESP) {
		/* fast response */
		srbreply->srb_status = cpu_to_le32(SRB_STATUS_SUCCESS);
		srbreply->scsi_status = cpu_to_le32(SAM_STAT_GOOD);
	} else {
		/*
		 *	Calculate resid for sg
		 */
		scsi_set_resid(scsicmd, scsi_bufflen(scsicmd)
				   - le32_to_cpu(srbreply->data_xfer_length));
	}


	scsi_dma_unmap(scsicmd);

	/* expose physical device if expose_physicald flag is on */
	if (scsicmd->cmnd[0] == INQUIRY && !(scsicmd->cmnd[1] & 0x01)
	  && expose_physicals > 0)
		aac_expose_phy_device(scsicmd);

	/*
	 * First check the fib status
	 */

	if (le32_to_cpu(srbreply->status) != ST_OK) {
		int len;

		pr_warn("aac_srb_callback: srb failed, status = %d\n",
				le32_to_cpu(srbreply->status));
		len = min_t(u32, le32_to_cpu(srbreply->sense_data_size),
			    SCSI_SENSE_BUFFERSIZE);
		scsicmd->result = DID_ERROR << 16
				| COMMAND_COMPLETE << 8
				| SAM_STAT_CHECK_CONDITION;
		memcpy(scsicmd->sense_buffer,
				srbreply->sense_data, len);
	}

	/*
	 * Next check the srb status
	 */
	switch ((le32_to_cpu(srbreply->srb_status))&0x3f) {
	case SRB_STATUS_ERROR_RECOVERY:
	case SRB_STATUS_PENDING:
	case SRB_STATUS_SUCCESS:
		scsicmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8;
		break;
	case SRB_STATUS_DATA_OVERRUN:
		switch (scsicmd->cmnd[0]) {
		case  READ_6:
		case  WRITE_6:
		case  READ_10:
		case  WRITE_10:
		case  READ_12:
		case  WRITE_12:
		case  READ_16:
		case  WRITE_16:
			if (le32_to_cpu(srbreply->data_xfer_length)
						< scsicmd->underflow)
				pr_warn("aacraid: SCSI CMD underflow\n");
			else
				pr_warn("aacraid: SCSI CMD Data Overrun\n");
			scsicmd->result = DID_ERROR << 16
					| COMMAND_COMPLETE << 8;
			break;
		case INQUIRY:
			scsicmd->result = DID_OK << 16
					| COMMAND_COMPLETE << 8;
			break;
		default:
			scsicmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8;
			break;
		}
		break;
	case SRB_STATUS_ABORTED:
		scsicmd->result = DID_ABORT << 16 | ABORT << 8;
		break;
	case SRB_STATUS_ABORT_FAILED:
		/*
		 * Not sure about this one - but assuming the
		 * hba was trying to abort for some reason
		 */
		scsicmd->result = DID_ERROR << 16 | ABORT << 8;
		break;
	case SRB_STATUS_PARITY_ERROR:
		scsicmd->result = DID_PARITY << 16
				| MSG_PARITY_ERROR << 8;
		break;
	case SRB_STATUS_NO_DEVICE:
	case SRB_STATUS_INVALID_PATH_ID:
	case SRB_STATUS_INVALID_TARGET_ID:
	case SRB_STATUS_INVALID_LUN:
	case SRB_STATUS_SELECTION_TIMEOUT:
		scsicmd->result = DID_NO_CONNECT << 16
				| COMMAND_COMPLETE << 8;
		break;

	case SRB_STATUS_COMMAND_TIMEOUT:
	case SRB_STATUS_TIMEOUT:
		scsicmd->result = DID_TIME_OUT << 16
				| COMMAND_COMPLETE << 8;
		break;

	case SRB_STATUS_BUSY:
		scsicmd->result = DID_BUS_BUSY << 16
				| COMMAND_COMPLETE << 8;
		break;

	case SRB_STATUS_BUS_RESET:
		scsicmd->result = DID_RESET << 16
				| COMMAND_COMPLETE << 8;
		break;

	case SRB_STATUS_MESSAGE_REJECTED:
		scsicmd->result = DID_ERROR << 16
				| MESSAGE_REJECT << 8;
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
		pr_info("aacraid: SRB ERROR(%u) %s scsi cmd 0x%x -scsi status 0x%x\n",
			le32_to_cpu(srbreply->srb_status) & 0x3F,
			aac_get_status_string(
				le32_to_cpu(srbreply->srb_status) & 0x3F),
			scsicmd->cmnd[0],
			le32_to_cpu(srbreply->scsi_status));
#endif
		/*
		 * When the CC bit is SET by the host in ATA pass thru CDB,
		 *  driver is supposed to return DID_OK
		 *
		 * When the CC bit is RESET by the host, driver should
		 *  return DID_ERROR
		 */
		if ((scsicmd->cmnd[0] == ATA_12)
			|| (scsicmd->cmnd[0] == ATA_16)) {

			if (scsicmd->cmnd[2] & (0x01 << 5)) {
				scsicmd->result = DID_OK << 16
					| COMMAND_COMPLETE << 8;
			break;
			} else {
				scsicmd->result = DID_ERROR << 16
					| COMMAND_COMPLETE << 8;
			break;
			}
		} else {
			scsicmd->result = DID_ERROR << 16
				| COMMAND_COMPLETE << 8;
			break;
		}
	}
	if (le32_to_cpu(srbreply->scsi_status)
			== SAM_STAT_CHECK_CONDITION) {
		int len;

		scsicmd->result |= SAM_STAT_CHECK_CONDITION;
		len = min_t(u32, le32_to_cpu(srbreply->sense_data_size),
			    SCSI_SENSE_BUFFERSIZE);
#ifdef AAC_DETAILED_STATUS_INFO
		pr_warn("aac_srb_callback: check condition, status = %d len=%d\n",
					le32_to_cpu(srbreply->status), len);
#endif
		memcpy(scsicmd->sense_buffer,
				srbreply->sense_data, len);
	}

	/*
	 * OR in the scsi status (already shifted up a bit)
	 */
	scsicmd->result |= le32_to_cpu(srbreply->scsi_status);

	aac_fib_complete(fibptr);
	scsicmd->scsi_done(scsicmd);
}

static void hba_resp_task_complete(struct aac_dev *dev,
					struct scsi_cmnd *scsicmd,
					struct aac_hba_resp *err) {

	scsicmd->result = err->status;
	/* set residual count */
	scsi_set_resid(scsicmd, le32_to_cpu(err->residual_count));

	switch (err->status) {
	case SAM_STAT_GOOD:
		scsicmd->result |= DID_OK << 16 | COMMAND_COMPLETE << 8;
		break;
	case SAM_STAT_CHECK_CONDITION:
	{
		int len;

		len = min_t(u8, err->sense_response_data_len,
			SCSI_SENSE_BUFFERSIZE);
		if (len)
			memcpy(scsicmd->sense_buffer,
				err->sense_response_buf, len);
		scsicmd->result |= DID_OK << 16 | COMMAND_COMPLETE << 8;
		break;
	}
	case SAM_STAT_BUSY:
		scsicmd->result |= DID_BUS_BUSY << 16 | COMMAND_COMPLETE << 8;
		break;
	case SAM_STAT_TASK_ABORTED:
		scsicmd->result |= DID_ABORT << 16 | ABORT << 8;
		break;
	case SAM_STAT_RESERVATION_CONFLICT:
	case SAM_STAT_TASK_SET_FULL:
	default:
		scsicmd->result |= DID_ERROR << 16 | COMMAND_COMPLETE << 8;
		break;
	}
}

static void hba_resp_task_failure(struct aac_dev *dev,
					struct scsi_cmnd *scsicmd,
					struct aac_hba_resp *err)
{
	switch (err->status) {
	case HBA_RESP_STAT_HBAMODE_DISABLED:
	{
		u32 bus, cid;

		bus = aac_logical_to_phys(scmd_channel(scsicmd));
		cid = scmd_id(scsicmd);
		if (dev->hba_map[bus][cid].devtype == AAC_DEVTYPE_NATIVE_RAW) {
			dev->hba_map[bus][cid].devtype = AAC_DEVTYPE_ARC_RAW;
			dev->hba_map[bus][cid].rmw_nexus = 0xffffffff;
		}
		scsicmd->result = DID_NO_CONNECT << 16 | COMMAND_COMPLETE << 8;
		break;
	}
	case HBA_RESP_STAT_IO_ERROR:
	case HBA_RESP_STAT_NO_PATH_TO_DEVICE:
		scsicmd->result = DID_OK << 16 |
			COMMAND_COMPLETE << 8 | SAM_STAT_BUSY;
		break;
	case HBA_RESP_STAT_IO_ABORTED:
		scsicmd->result = DID_ABORT << 16 | ABORT << 8;
		break;
	case HBA_RESP_STAT_INVALID_DEVICE:
		scsicmd->result = DID_NO_CONNECT << 16 | COMMAND_COMPLETE << 8;
		break;
	case HBA_RESP_STAT_UNDERRUN:
		/* UNDERRUN is OK */
		scsicmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8;
		break;
	case HBA_RESP_STAT_OVERRUN:
	default:
		scsicmd->result = DID_ERROR << 16 | COMMAND_COMPLETE << 8;
		break;
	}
}

/**
 *
 * aac_hba_callback
 * @context: the context set in the fib - here it is scsi cmd
 * @fibptr: pointer to the fib
 *
 * Handles the completion of a native HBA scsi command
 *
 */
void aac_hba_callback(void *context, struct fib *fibptr)
{
	struct aac_dev *dev;
	struct scsi_cmnd *scsicmd;

	struct aac_hba_resp *err =
			&((struct aac_native_hba *)fibptr->hw_fib_va)->resp.err;

	scsicmd = (struct scsi_cmnd *) context;

	if (!aac_valid_context(scsicmd, fibptr))
		return;

	WARN_ON(fibptr == NULL);
	dev = fibptr->dev;

	if (!(fibptr->flags & FIB_CONTEXT_FLAG_NATIVE_HBA_TMF))
		scsi_dma_unmap(scsicmd);

	if (fibptr->flags & FIB_CONTEXT_FLAG_FASTRESP) {
		/* fast response */
		scsicmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8;
		goto out;
	}

	switch (err->service_response) {
	case HBA_RESP_SVCRES_TASK_COMPLETE:
		hba_resp_task_complete(dev, scsicmd, err);
		break;
	case HBA_RESP_SVCRES_FAILURE:
		hba_resp_task_failure(dev, scsicmd, err);
		break;
	case HBA_RESP_SVCRES_TMF_REJECTED:
		scsicmd->result = DID_ERROR << 16 | MESSAGE_REJECT << 8;
		break;
	case HBA_RESP_SVCRES_TMF_LUN_INVALID:
		scsicmd->result = DID_NO_CONNECT << 16 | COMMAND_COMPLETE << 8;
		break;
	case HBA_RESP_SVCRES_TMF_COMPLETE:
	case HBA_RESP_SVCRES_TMF_SUCCEEDED:
		scsicmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8;
		break;
	default:
		scsicmd->result = DID_ERROR << 16 | COMMAND_COMPLETE << 8;
		break;
	}

out:
	aac_fib_complete(fibptr);

	if (fibptr->flags & FIB_CONTEXT_FLAG_NATIVE_HBA_TMF)
		scsicmd->SCp.sent_command = 1;
	else
		scsicmd->scsi_done(scsicmd);
}

/**
 *
 * aac_send_srb_fib
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
	cmd_fibcontext = aac_fib_alloc_tag(dev, scsicmd);
	scsicmd->SCp.phase = AAC_OWNER_FIRMWARE;
	status = aac_adapter_scsi(cmd_fibcontext, scsicmd);

	/*
	 *	Check that the command queued to the controller
	 */
	if (status == -EINPROGRESS)
		return 0;

	printk(KERN_WARNING "aac_srb: aac_fib_send failed with status: %d\n", status);
	aac_fib_complete(cmd_fibcontext);
	aac_fib_free(cmd_fibcontext);

	return -1;
}

/**
 *
 * aac_send_hba_fib
 * @scsicmd: the scsi command block
 *
 * This routine will form a FIB and fill in the aac_hba_cmd_req from the
 * scsicmd passed in.
 */
static int aac_send_hba_fib(struct scsi_cmnd *scsicmd)
{
	struct fib *cmd_fibcontext;
	struct aac_dev *dev;
	int status;

	dev = shost_priv(scsicmd->device->host);
	if (scmd_id(scsicmd) >= dev->maximum_num_physicals ||
			scsicmd->device->lun > AAC_MAX_LUN - 1) {
		scsicmd->result = DID_NO_CONNECT << 16;
		scsicmd->scsi_done(scsicmd);
		return 0;
	}

	/*
	 *	Allocate and initialize a Fib then setup a BlockWrite command
	 */
	cmd_fibcontext = aac_fib_alloc_tag(dev, scsicmd);
	if (!cmd_fibcontext)
		return -1;

	scsicmd->SCp.phase = AAC_OWNER_FIRMWARE;
	status = aac_adapter_hba(cmd_fibcontext, scsicmd);

	/*
	 *	Check that the command queued to the controller
	 */
	if (status == -EINPROGRESS)
		return 0;

	pr_warn("aac_hba_cmd_req: aac_fib_send failed with status: %d\n",
		status);
	aac_fib_complete(cmd_fibcontext);
	aac_fib_free(cmd_fibcontext);

	return -1;
}


static long aac_build_sg(struct scsi_cmnd *scsicmd, struct sgmap *psg)
{
	unsigned long byte_count = 0;
	int nseg;
	struct scatterlist *sg;
	int i;

	// Get rid of old data
	psg->count = 0;
	psg->sg[0].addr = 0;
	psg->sg[0].count = 0;

	nseg = scsi_dma_map(scsicmd);
	if (nseg <= 0)
		return nseg;

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
	if (scsicmd->underflow && (byte_count < scsicmd->underflow)) {
		printk(KERN_WARNING"aacraid: cmd len %08lX cmd underflow %08X\n",
		       byte_count, scsicmd->underflow);
	}

	return byte_count;
}


static long aac_build_sg64(struct scsi_cmnd *scsicmd, struct sgmap64 *psg)
{
	unsigned long byte_count = 0;
	u64 addr;
	int nseg;
	struct scatterlist *sg;
	int i;

	// Get rid of old data
	psg->count = 0;
	psg->sg[0].addr[0] = 0;
	psg->sg[0].addr[1] = 0;
	psg->sg[0].count = 0;

	nseg = scsi_dma_map(scsicmd);
	if (nseg <= 0)
		return nseg;

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
	if (scsicmd->underflow && (byte_count < scsicmd->underflow)) {
		printk(KERN_WARNING"aacraid: cmd len %08lX cmd underflow %08X\n",
		       byte_count, scsicmd->underflow);
	}

	return byte_count;
}

static long aac_build_sgraw(struct scsi_cmnd *scsicmd, struct sgmapraw *psg)
{
	unsigned long byte_count = 0;
	int nseg;
	struct scatterlist *sg;
	int i;

	// Get rid of old data
	psg->count = 0;
	psg->sg[0].next = 0;
	psg->sg[0].prev = 0;
	psg->sg[0].addr[0] = 0;
	psg->sg[0].addr[1] = 0;
	psg->sg[0].count = 0;
	psg->sg[0].flags = 0;

	nseg = scsi_dma_map(scsicmd);
	if (nseg <= 0)
		return nseg;

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
	if (scsicmd->underflow && (byte_count < scsicmd->underflow)) {
		printk(KERN_WARNING"aacraid: cmd len %08lX cmd underflow %08X\n",
		       byte_count, scsicmd->underflow);
	}

	return byte_count;
}

static long aac_build_sgraw2(struct scsi_cmnd *scsicmd,
				struct aac_raw_io2 *rio2, int sg_max)
{
	unsigned long byte_count = 0;
	int nseg;
	struct scatterlist *sg;
	int i, conformable = 0;
	u32 min_size = PAGE_SIZE, cur_size;

	nseg = scsi_dma_map(scsicmd);
	if (nseg <= 0)
		return nseg;

	scsi_for_each_sg(scsicmd, sg, nseg, i) {
		int count = sg_dma_len(sg);
		u64 addr = sg_dma_address(sg);

		BUG_ON(i >= sg_max);
		rio2->sge[i].addrHigh = cpu_to_le32((u32)(addr>>32));
		rio2->sge[i].addrLow = cpu_to_le32((u32)(addr & 0xffffffff));
		cur_size = cpu_to_le32(count);
		rio2->sge[i].length = cur_size;
		rio2->sge[i].flags = 0;
		if (i == 0) {
			conformable = 1;
			rio2->sgeFirstSize = cur_size;
		} else if (i == 1) {
			rio2->sgeNominalSize = cur_size;
			min_size = cur_size;
		} else if ((i+1) < nseg && cur_size != rio2->sgeNominalSize) {
			conformable = 0;
			if (cur_size < min_size)
				min_size = cur_size;
		}
		byte_count += count;
	}

	/* hba wants the size to be exact */
	if (byte_count > scsi_bufflen(scsicmd)) {
		u32 temp = le32_to_cpu(rio2->sge[i-1].length) -
			(byte_count - scsi_bufflen(scsicmd));
		rio2->sge[i-1].length = cpu_to_le32(temp);
		byte_count = scsi_bufflen(scsicmd);
	}

	rio2->sgeCnt = cpu_to_le32(nseg);
	rio2->flags |= cpu_to_le16(RIO2_SG_FORMAT_IEEE1212);
	/* not conformable: evaluate required sg elements */
	if (!conformable) {
		int j, nseg_new = nseg, err_found;
		for (i = min_size / PAGE_SIZE; i >= 1; --i) {
			err_found = 0;
			nseg_new = 2;
			for (j = 1; j < nseg - 1; ++j) {
				if (rio2->sge[j].length % (i*PAGE_SIZE)) {
					err_found = 1;
					break;
				}
				nseg_new += (rio2->sge[j].length / (i*PAGE_SIZE));
			}
			if (!err_found)
				break;
		}
		if (i > 0 && nseg_new <= sg_max) {
			int ret = aac_convert_sgraw2(rio2, i, nseg, nseg_new);

			if (ret < 0)
				return ret;
		}
	} else
		rio2->flags |= cpu_to_le16(RIO2_SGL_CONFORMANT);

	/* Check for command underflow */
	if (scsicmd->underflow && (byte_count < scsicmd->underflow)) {
		printk(KERN_WARNING"aacraid: cmd len %08lX cmd underflow %08X\n",
		       byte_count, scsicmd->underflow);
	}

	return byte_count;
}

static int aac_convert_sgraw2(struct aac_raw_io2 *rio2, int pages, int nseg, int nseg_new)
{
	struct sge_ieee1212 *sge;
	int i, j, pos;
	u32 addr_low;

	if (aac_convert_sgl == 0)
		return 0;

	sge = kmalloc_array(nseg_new, sizeof(struct sge_ieee1212), GFP_ATOMIC);
	if (sge == NULL)
		return -ENOMEM;

	for (i = 1, pos = 1; i < nseg-1; ++i) {
		for (j = 0; j < rio2->sge[i].length / (pages * PAGE_SIZE); ++j) {
			addr_low = rio2->sge[i].addrLow + j * pages * PAGE_SIZE;
			sge[pos].addrLow = addr_low;
			sge[pos].addrHigh = rio2->sge[i].addrHigh;
			if (addr_low < rio2->sge[i].addrLow)
				sge[pos].addrHigh++;
			sge[pos].length = pages * PAGE_SIZE;
			sge[pos].flags = 0;
			pos++;
		}
	}
	sge[pos] = rio2->sge[nseg-1];
	memcpy(&rio2->sge[1], &sge[1], (nseg_new-1)*sizeof(struct sge_ieee1212));

	kfree(sge);
	rio2->sgeCnt = cpu_to_le32(nseg_new);
	rio2->flags |= cpu_to_le16(RIO2_SGL_CONFORMANT);
	rio2->sgeNominalSize = pages * PAGE_SIZE;
	return 0;
}

static long aac_build_sghba(struct scsi_cmnd *scsicmd,
			struct aac_hba_cmd_req *hbacmd,
			int sg_max,
			u64 sg_address)
{
	unsigned long byte_count = 0;
	int nseg;
	struct scatterlist *sg;
	int i;
	u32 cur_size;
	struct aac_hba_sgl *sge;

	nseg = scsi_dma_map(scsicmd);
	if (nseg <= 0) {
		byte_count = nseg;
		goto out;
	}

	if (nseg > HBA_MAX_SG_EMBEDDED)
		sge = &hbacmd->sge[2];
	else
		sge = &hbacmd->sge[0];

	scsi_for_each_sg(scsicmd, sg, nseg, i) {
		int count = sg_dma_len(sg);
		u64 addr = sg_dma_address(sg);

		WARN_ON(i >= sg_max);
		sge->addr_hi = cpu_to_le32((u32)(addr>>32));
		sge->addr_lo = cpu_to_le32((u32)(addr & 0xffffffff));
		cur_size = cpu_to_le32(count);
		sge->len = cur_size;
		sge->flags = 0;
		byte_count += count;
		sge++;
	}

	sge--;
	/* hba wants the size to be exact */
	if (byte_count > scsi_bufflen(scsicmd)) {
		u32 temp;

		temp = le32_to_cpu(sge->len) - byte_count
						- scsi_bufflen(scsicmd);
		sge->len = cpu_to_le32(temp);
		byte_count = scsi_bufflen(scsicmd);
	}

	if (nseg <= HBA_MAX_SG_EMBEDDED) {
		hbacmd->emb_data_desc_count = cpu_to_le32(nseg);
		sge->flags = cpu_to_le32(0x40000000);
	} else {
		/* not embedded */
		hbacmd->sge[0].flags = cpu_to_le32(0x80000000);
		hbacmd->emb_data_desc_count = (u8)cpu_to_le32(1);
		hbacmd->sge[0].addr_hi = (u32)cpu_to_le32(sg_address >> 32);
		hbacmd->sge[0].addr_lo =
			cpu_to_le32((u32)(sg_address & 0xffffffff));
	}

	/* Check for command underflow */
	if (scsicmd->underflow && (byte_count < scsicmd->underflow)) {
		pr_warn("aacraid: cmd len %08lX cmd underflow %08X\n",
				byte_count, scsicmd->underflow);
	}
out:
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
