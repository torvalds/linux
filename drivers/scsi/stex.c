/*
 * SuperTrak EX Series Storage Controller driver for Linux
 *
 *	Copyright (C) 2005, 2006 Promise Technology Inc.
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *	Written By:
 *		Ed Lin <promise_linux@promise.com>
 *
 *	Version: 3.0.0.1
 *
 */

#include <linux/init.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/time.h>
#include <linux/pci.h>
#include <linux/blkdev.h>
#include <linux/interrupt.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/byteorder.h>
#include <scsi/scsi.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_tcq.h>

#define DRV_NAME "stex"
#define ST_DRIVER_VERSION "3.0.0.1"
#define ST_VER_MAJOR 		3
#define ST_VER_MINOR 		0
#define ST_OEM 			0
#define ST_BUILD_VER 		1

enum {
	/* MU register offset */
	IMR0	= 0x10,	/* MU_INBOUND_MESSAGE_REG0 */
	IMR1	= 0x14,	/* MU_INBOUND_MESSAGE_REG1 */
	OMR0	= 0x18,	/* MU_OUTBOUND_MESSAGE_REG0 */
	OMR1	= 0x1c,	/* MU_OUTBOUND_MESSAGE_REG1 */
	IDBL	= 0x20,	/* MU_INBOUND_DOORBELL */
	IIS	= 0x24,	/* MU_INBOUND_INTERRUPT_STATUS */
	IIM	= 0x28,	/* MU_INBOUND_INTERRUPT_MASK */
	ODBL	= 0x2c,	/* MU_OUTBOUND_DOORBELL */
	OIS	= 0x30,	/* MU_OUTBOUND_INTERRUPT_STATUS */
	OIM	= 0x3c,	/* MU_OUTBOUND_INTERRUPT_MASK */

	/* MU register value */
	MU_INBOUND_DOORBELL_HANDSHAKE		= 1,
	MU_INBOUND_DOORBELL_REQHEADCHANGED	= 2,
	MU_INBOUND_DOORBELL_STATUSTAILCHANGED	= 4,
	MU_INBOUND_DOORBELL_HMUSTOPPED		= 8,
	MU_INBOUND_DOORBELL_RESET		= 16,

	MU_OUTBOUND_DOORBELL_HANDSHAKE		= 1,
	MU_OUTBOUND_DOORBELL_REQUESTTAILCHANGED	= 2,
	MU_OUTBOUND_DOORBELL_STATUSHEADCHANGED	= 4,
	MU_OUTBOUND_DOORBELL_BUSCHANGE		= 8,
	MU_OUTBOUND_DOORBELL_HASEVENT		= 16,

	/* MU status code */
	MU_STATE_STARTING			= 1,
	MU_STATE_FMU_READY_FOR_HANDSHAKE	= 2,
	MU_STATE_SEND_HANDSHAKE_FRAME		= 3,
	MU_STATE_STARTED			= 4,
	MU_STATE_RESETTING			= 5,

	MU_MAX_DELAY_TIME			= 240000,
	MU_HANDSHAKE_SIGNATURE			= 0x55aaaa55,
	HMU_PARTNER_TYPE			= 2,

	/* firmware returned values */
	SRB_STATUS_SUCCESS			= 0x01,
	SRB_STATUS_ERROR			= 0x04,
	SRB_STATUS_BUSY				= 0x05,
	SRB_STATUS_INVALID_REQUEST		= 0x06,
	SRB_STATUS_SELECTION_TIMEOUT		= 0x0A,
	SRB_SEE_SENSE 				= 0x80,

	/* task attribute */
	TASK_ATTRIBUTE_SIMPLE			= 0x0,
	TASK_ATTRIBUTE_HEADOFQUEUE		= 0x1,
	TASK_ATTRIBUTE_ORDERED			= 0x2,
	TASK_ATTRIBUTE_ACA			= 0x4,

	/* request count, etc. */
	MU_MAX_REQUEST				= 32,

	/* one message wasted, use MU_MAX_REQUEST+1
		to handle MU_MAX_REQUEST messages */
	MU_REQ_COUNT				= (MU_MAX_REQUEST + 1),
	MU_STATUS_COUNT				= (MU_MAX_REQUEST + 1),

	STEX_CDB_LENGTH				= MAX_COMMAND_SIZE,
	REQ_VARIABLE_LEN			= 1024,
	STATUS_VAR_LEN				= 128,
	ST_CAN_QUEUE				= MU_MAX_REQUEST,
	ST_CMD_PER_LUN				= MU_MAX_REQUEST,
	ST_MAX_SG				= 32,

	/* sg flags */
	SG_CF_EOT				= 0x80,	/* end of table */
	SG_CF_64B				= 0x40,	/* 64 bit item */
	SG_CF_HOST				= 0x20,	/* sg in host memory */

	ST_MAX_ARRAY_SUPPORTED			= 16,
	ST_MAX_TARGET_NUM			= (ST_MAX_ARRAY_SUPPORTED+1),
	ST_MAX_LUN_PER_TARGET			= 16,

	st_shasta				= 0,
	st_vsc					= 1,
	st_yosemite				= 2,

	PASSTHRU_REQ_TYPE			= 0x00000001,
	PASSTHRU_REQ_NO_WAKEUP			= 0x00000100,
	ST_INTERNAL_TIMEOUT			= 30,

	ST_TO_CMD				= 0,
	ST_FROM_CMD				= 1,

	/* vendor specific commands of Promise */
	MGT_CMD					= 0xd8,
	SINBAND_MGT_CMD				= 0xd9,
	ARRAY_CMD				= 0xe0,
	CONTROLLER_CMD				= 0xe1,
	DEBUGGING_CMD				= 0xe2,
	PASSTHRU_CMD				= 0xe3,

	PASSTHRU_GET_ADAPTER			= 0x05,
	PASSTHRU_GET_DRVVER			= 0x10,

	CTLR_CONFIG_CMD				= 0x03,
	CTLR_SHUTDOWN				= 0x0d,

	CTLR_POWER_STATE_CHANGE			= 0x0e,
	CTLR_POWER_SAVING			= 0x01,

	PASSTHRU_SIGNATURE			= 0x4e415041,
	MGT_CMD_SIGNATURE			= 0xba,

	INQUIRY_EVPD				= 0x01,
};

/* SCSI inquiry data */
typedef struct st_inq {
	u8 DeviceType			:5;
	u8 DeviceTypeQualifier		:3;
	u8 DeviceTypeModifier		:7;
	u8 RemovableMedia		:1;
	u8 Versions;
	u8 ResponseDataFormat		:4;
	u8 HiSupport			:1;
	u8 NormACA			:1;
	u8 ReservedBit			:1;
	u8 AERC				:1;
	u8 AdditionalLength;
	u8 Reserved[2];
	u8 SoftReset			:1;
	u8 CommandQueue			:1;
	u8 Reserved2			:1;
	u8 LinkedCommands		:1;
	u8 Synchronous			:1;
	u8 Wide16Bit			:1;
	u8 Wide32Bit			:1;
	u8 RelativeAddressing		:1;
	u8 VendorId[8];
	u8 ProductId[16];
	u8 ProductRevisionLevel[4];
	u8 VendorSpecific[20];
	u8 Reserved3[40];
} ST_INQ;

struct st_sgitem {
	u8 ctrl;	/* SG_CF_xxx */
	u8 reserved[3];
	__le32 count;
	__le32 addr;
	__le32 addr_hi;
};

struct st_sgtable {
	__le16 sg_count;
	__le16 max_sg_count;
	__le32 sz_in_byte;
	struct st_sgitem table[ST_MAX_SG];
};

struct handshake_frame {
	__le32 rb_phy;		/* request payload queue physical address */
	__le32 rb_phy_hi;
	__le16 req_sz;		/* size of each request payload */
	__le16 req_cnt;		/* count of reqs the buffer can hold */
	__le16 status_sz;	/* size of each status payload */
	__le16 status_cnt;	/* count of status the buffer can hold */
	__le32 hosttime;	/* seconds from Jan 1, 1970 (GMT) */
	__le32 hosttime_hi;
	u8 partner_type;	/* who sends this frame */
	u8 reserved0[7];
	__le32 partner_ver_major;
	__le32 partner_ver_minor;
	__le32 partner_ver_oem;
	__le32 partner_ver_build;
	u32 reserved1[4];
};

struct req_msg {
	__le16 tag;
	u8 lun;
	u8 target;
	u8 task_attr;
	u8 task_manage;
	u8 prd_entry;
	u8 payload_sz;		/* payload size in 4-byte, not used */
	u8 cdb[STEX_CDB_LENGTH];
	u8 variable[REQ_VARIABLE_LEN];
};

struct status_msg {
	__le16 tag;
	u8 lun;
	u8 target;
	u8 srb_status;
	u8 scsi_status;
	u8 reserved;
	u8 payload_sz;		/* payload size in 4-byte */
	u8 variable[STATUS_VAR_LEN];
};

struct ver_info {
	u32 major;
	u32 minor;
	u32 oem;
	u32 build;
	u32 reserved[2];
};

struct st_frame {
	u32 base[6];
	u32 rom_addr;

	struct ver_info drv_ver;
	struct ver_info bios_ver;

	u32 bus;
	u32 slot;
	u32 irq_level;
	u32 irq_vec;
	u32 id;
	u32 subid;

	u32 dimm_size;
	u8 dimm_type;
	u8 reserved[3];

	u32 channel;
	u32 reserved1;
};

struct st_drvver {
	u32 major;
	u32 minor;
	u32 oem;
	u32 build;
	u32 signature[2];
	u8 console_id;
	u8 host_no;
	u8 reserved0[2];
	u32 reserved[3];
};

#define MU_REQ_BUFFER_SIZE	(MU_REQ_COUNT * sizeof(struct req_msg))
#define MU_STATUS_BUFFER_SIZE	(MU_STATUS_COUNT * sizeof(struct status_msg))
#define MU_BUFFER_SIZE		(MU_REQ_BUFFER_SIZE + MU_STATUS_BUFFER_SIZE)
#define STEX_EXTRA_SIZE		max(sizeof(struct st_frame), sizeof(ST_INQ))
#define STEX_BUFFER_SIZE	(MU_BUFFER_SIZE + STEX_EXTRA_SIZE)

struct st_ccb {
	struct req_msg *req;
	struct scsi_cmnd *cmd;

	void *sense_buffer;
	unsigned int sense_bufflen;
	int sg_count;

	u32 req_type;
	u8 srb_status;
	u8 scsi_status;
};

struct st_hba {
	void __iomem *mmio_base;	/* iomapped PCI memory space */
	void *dma_mem;
	dma_addr_t dma_handle;

	struct Scsi_Host *host;
	struct pci_dev *pdev;

	u32 req_head;
	u32 req_tail;
	u32 status_head;
	u32 status_tail;

	struct status_msg *status_buffer;
	void *copy_buffer; /* temp buffer for driver-handled commands */
	struct st_ccb ccb[MU_MAX_REQUEST];
	struct st_ccb *wait_ccb;
	wait_queue_head_t waitq;

	unsigned int mu_status;
	int out_req_cnt;

	unsigned int cardtype;
};

static const char console_inq_page[] =
{
	0x03,0x00,0x03,0x03,0xFA,0x00,0x00,0x30,
	0x50,0x72,0x6F,0x6D,0x69,0x73,0x65,0x20,	/* "Promise " */
	0x52,0x41,0x49,0x44,0x20,0x43,0x6F,0x6E,	/* "RAID Con" */
	0x73,0x6F,0x6C,0x65,0x20,0x20,0x20,0x20,	/* "sole    " */
	0x31,0x2E,0x30,0x30,0x20,0x20,0x20,0x20,	/* "1.00    " */
	0x53,0x58,0x2F,0x52,0x53,0x41,0x46,0x2D,	/* "SX/RSAF-" */
	0x54,0x45,0x31,0x2E,0x30,0x30,0x20,0x20,	/* "TE1.00  " */
	0x0C,0x20,0x20,0x20,0x20,0x20,0x20,0x20
};

MODULE_AUTHOR("Ed Lin");
MODULE_DESCRIPTION("Promise Technology SuperTrak EX Controllers");
MODULE_LICENSE("GPL");
MODULE_VERSION(ST_DRIVER_VERSION);

static void stex_gettime(__le32 *time)
{
	struct timeval tv;
	do_gettimeofday(&tv);

	*time = cpu_to_le32(tv.tv_sec & 0xffffffff);
	*(time + 1) = cpu_to_le32((tv.tv_sec >> 16) >> 16);
}

static struct status_msg *stex_get_status(struct st_hba *hba)
{
	struct status_msg *status =
		hba->status_buffer + hba->status_tail;

	++hba->status_tail;
	hba->status_tail %= MU_STATUS_COUNT;

	return status;
}

static void stex_set_sense(struct scsi_cmnd *cmd, u8 sk, u8 asc, u8 ascq)
{
	cmd->result = (DRIVER_SENSE << 24) | SAM_STAT_CHECK_CONDITION;

	cmd->sense_buffer[0] = 0x70;    /* fixed format, current */
	cmd->sense_buffer[2] = sk;
	cmd->sense_buffer[7] = 18 - 8;  /* additional sense length */
	cmd->sense_buffer[12] = asc;
	cmd->sense_buffer[13] = ascq;
}

static void stex_invalid_field(struct scsi_cmnd *cmd,
			       void (*done)(struct scsi_cmnd *))
{
	/* "Invalid field in cbd" */
	stex_set_sense(cmd, ILLEGAL_REQUEST, 0x24, 0x0);
	done(cmd);
}

static struct req_msg *stex_alloc_req(struct st_hba *hba)
{
	struct req_msg *req = ((struct req_msg *)hba->dma_mem) +
		hba->req_head;

	++hba->req_head;
	hba->req_head %= MU_REQ_COUNT;

	return req;
}

static int stex_map_sg(struct st_hba *hba,
	struct req_msg *req, struct st_ccb *ccb)
{
	struct pci_dev *pdev = hba->pdev;
	struct scsi_cmnd *cmd;
	dma_addr_t dma_handle;
	struct scatterlist *src;
	struct st_sgtable *dst;
	int i;

	cmd = ccb->cmd;
	dst = (struct st_sgtable *)req->variable;
	dst->max_sg_count = cpu_to_le16(ST_MAX_SG);
	dst->sz_in_byte = cpu_to_le32(cmd->request_bufflen);

	if (cmd->use_sg) {
		int n_elem;

		src = (struct scatterlist *) cmd->request_buffer;
		n_elem = pci_map_sg(pdev, src,
			cmd->use_sg, cmd->sc_data_direction);
		if (n_elem <= 0)
			return -EIO;

		ccb->sg_count = n_elem;
		dst->sg_count = cpu_to_le16((u16)n_elem);

		for (i = 0; i < n_elem; i++, src++) {
			dst->table[i].count = cpu_to_le32((u32)sg_dma_len(src));
			dst->table[i].addr =
				cpu_to_le32(sg_dma_address(src) & 0xffffffff);
			dst->table[i].addr_hi =
				cpu_to_le32((sg_dma_address(src) >> 16) >> 16);
			dst->table[i].ctrl = SG_CF_64B | SG_CF_HOST;
		}
		dst->table[--i].ctrl |= SG_CF_EOT;
		return 0;
	}

	dma_handle = pci_map_single(pdev, cmd->request_buffer,
		cmd->request_bufflen, cmd->sc_data_direction);
	cmd->SCp.dma_handle = dma_handle;

	ccb->sg_count = 1;
	dst->sg_count = cpu_to_le16(1);
	dst->table[0].addr = cpu_to_le32(dma_handle & 0xffffffff);
	dst->table[0].addr_hi = cpu_to_le32((dma_handle >> 16) >> 16);
	dst->table[0].count = cpu_to_le32((u32)cmd->request_bufflen);
	dst->table[0].ctrl = SG_CF_EOT | SG_CF_64B | SG_CF_HOST;

	return 0;
}

static void stex_internal_copy(struct scsi_cmnd *cmd,
	const void *src, size_t *count, int sg_count, int direction)
{
	size_t lcount;
	size_t len;
	void *s, *d, *base = NULL;
	if (*count > cmd->request_bufflen)
		*count = cmd->request_bufflen;
	lcount = *count;
	while (lcount) {
		len = lcount;
		s = (void *)src;
		if (cmd->use_sg) {
			size_t offset = *count - lcount;
			s += offset;
			base = scsi_kmap_atomic_sg(cmd->request_buffer,
				sg_count, &offset, &len);
			if (base == NULL) {
				*count -= lcount;
				return;
			}
			d = base + offset;
		} else
			d = cmd->request_buffer;

		if (direction == ST_TO_CMD)
			memcpy(d, s, len);
		else
			memcpy(s, d, len);

		lcount -= len;
		if (cmd->use_sg)
			scsi_kunmap_atomic_sg(base);
	}
}

static int stex_direct_copy(struct scsi_cmnd *cmd,
	const void *src, size_t count)
{
	struct st_hba *hba = (struct st_hba *) &cmd->device->host->hostdata[0];
	size_t cp_len = count;
	int n_elem = 0;

	if (cmd->use_sg) {
		n_elem = pci_map_sg(hba->pdev, cmd->request_buffer,
			cmd->use_sg, cmd->sc_data_direction);
		if (n_elem <= 0)
			return 0;
	}

	stex_internal_copy(cmd, src, &cp_len, n_elem, ST_TO_CMD);

	if (cmd->use_sg)
		pci_unmap_sg(hba->pdev, cmd->request_buffer,
			cmd->use_sg, cmd->sc_data_direction);
	return cp_len == count;
}

static void stex_controller_info(struct st_hba *hba, struct st_ccb *ccb)
{
	struct st_frame *p;
	size_t count = sizeof(struct st_frame);

	p = hba->copy_buffer;
	memset(p->base, 0, sizeof(u32)*6);
	*(unsigned long *)(p->base) = pci_resource_start(hba->pdev, 0);
	p->rom_addr = 0;

	p->drv_ver.major = ST_VER_MAJOR;
	p->drv_ver.minor = ST_VER_MINOR;
	p->drv_ver.oem = ST_OEM;
	p->drv_ver.build = ST_BUILD_VER;

	p->bus = hba->pdev->bus->number;
	p->slot = hba->pdev->devfn;
	p->irq_level = 0;
	p->irq_vec = hba->pdev->irq;
	p->id = hba->pdev->vendor << 16 | hba->pdev->device;
	p->subid =
		hba->pdev->subsystem_vendor << 16 | hba->pdev->subsystem_device;

	stex_internal_copy(ccb->cmd, p, &count, ccb->sg_count, ST_TO_CMD);
}

static void
stex_send_cmd(struct st_hba *hba, struct req_msg *req, u16 tag)
{
	req->tag = cpu_to_le16(tag);
	req->task_attr = TASK_ATTRIBUTE_SIMPLE;
	req->task_manage = 0; /* not supported yet */

	hba->ccb[tag].req = req;
	hba->out_req_cnt++;

	writel(hba->req_head, hba->mmio_base + IMR0);
	writel(MU_INBOUND_DOORBELL_REQHEADCHANGED, hba->mmio_base + IDBL);
	readl(hba->mmio_base + IDBL); /* flush */
}

static int
stex_slave_alloc(struct scsi_device *sdev)
{
	/* Cheat: usually extracted from Inquiry data */
	sdev->tagged_supported = 1;

	scsi_activate_tcq(sdev, sdev->host->can_queue);

	return 0;
}

static int
stex_slave_config(struct scsi_device *sdev)
{
	sdev->use_10_for_rw = 1;
	sdev->use_10_for_ms = 1;
	sdev->timeout = 60 * HZ;
	sdev->tagged_supported = 1;

	return 0;
}

static void
stex_slave_destroy(struct scsi_device *sdev)
{
	scsi_deactivate_tcq(sdev, 1);
}

static int
stex_queuecommand(struct scsi_cmnd *cmd, void (* done)(struct scsi_cmnd *))
{
	struct st_hba *hba;
	struct Scsi_Host *host;
	unsigned int id,lun;
	struct req_msg *req;
	u16 tag;
	host = cmd->device->host;
	id = cmd->device->id;
	lun = cmd->device->channel; /* firmware lun issue work around */
	hba = (struct st_hba *) &host->hostdata[0];

	switch (cmd->cmnd[0]) {
	case MODE_SENSE_10:
	{
		static char ms10_caching_page[12] =
			{ 0, 0x12, 0, 0, 0, 0, 0, 0, 0x8, 0xa, 0x4, 0 };
		unsigned char page;
		page = cmd->cmnd[2] & 0x3f;
		if (page == 0x8 || page == 0x3f) {
			stex_direct_copy(cmd, ms10_caching_page,
					sizeof(ms10_caching_page));
			cmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8;
			done(cmd);
		} else
			stex_invalid_field(cmd, done);
		return 0;
	}
	case INQUIRY:
		if (id != ST_MAX_ARRAY_SUPPORTED)
			break;
		if (lun == 0 && (cmd->cmnd[1] & INQUIRY_EVPD) == 0) {
			stex_direct_copy(cmd, console_inq_page,
				sizeof(console_inq_page));
			cmd->result = DID_OK << 16 | COMMAND_COMPLETE << 8;
			done(cmd);
		} else
			stex_invalid_field(cmd, done);
		return 0;
	case PASSTHRU_CMD:
		if (cmd->cmnd[1] == PASSTHRU_GET_DRVVER) {
			struct st_drvver ver;
			ver.major = ST_VER_MAJOR;
			ver.minor = ST_VER_MINOR;
			ver.oem = ST_OEM;
			ver.build = ST_BUILD_VER;
			ver.signature[0] = PASSTHRU_SIGNATURE;
			ver.console_id = ST_MAX_ARRAY_SUPPORTED;
			ver.host_no = hba->host->host_no;
			cmd->result = stex_direct_copy(cmd, &ver, sizeof(ver)) ?
				DID_OK << 16 | COMMAND_COMPLETE << 8 :
				DID_ERROR << 16 | COMMAND_COMPLETE << 8;
			done(cmd);
			return 0;
		}
	default:
		break;
	}

	cmd->scsi_done = done;

	tag = cmd->request->tag;

	if (unlikely(tag >= host->can_queue))
		return SCSI_MLQUEUE_HOST_BUSY;

	req = stex_alloc_req(hba);

	if (hba->cardtype == st_yosemite) {
		req->lun = lun * (ST_MAX_TARGET_NUM - 1) + id;
		req->target = 0;
	} else {
		req->lun = lun;
		req->target = id;
	}

	/* cdb */
	memcpy(req->cdb, cmd->cmnd, STEX_CDB_LENGTH);

	hba->ccb[tag].cmd = cmd;
	hba->ccb[tag].sense_bufflen = SCSI_SENSE_BUFFERSIZE;
	hba->ccb[tag].sense_buffer = cmd->sense_buffer;
	hba->ccb[tag].req_type = 0;

	if (cmd->sc_data_direction != DMA_NONE)
		stex_map_sg(hba, req, &hba->ccb[tag]);

	stex_send_cmd(hba, req, tag);
	return 0;
}

static void stex_unmap_sg(struct st_hba *hba, struct scsi_cmnd *cmd)
{
	if (cmd->sc_data_direction != DMA_NONE) {
		if (cmd->use_sg)
			pci_unmap_sg(hba->pdev, cmd->request_buffer,
				cmd->use_sg, cmd->sc_data_direction);
		else
			pci_unmap_single(hba->pdev, cmd->SCp.dma_handle,
				cmd->request_bufflen, cmd->sc_data_direction);
	}
}

static void stex_scsi_done(struct st_ccb *ccb)
{
	struct scsi_cmnd *cmd = ccb->cmd;
	int result;

	if (ccb->srb_status == SRB_STATUS_SUCCESS ||  ccb->srb_status == 0) {
		result = ccb->scsi_status;
		switch (ccb->scsi_status) {
		case SAM_STAT_GOOD:
			result |= DID_OK << 16 | COMMAND_COMPLETE << 8;
			break;
		case SAM_STAT_CHECK_CONDITION:
			result |= DRIVER_SENSE << 24;
			break;
		case SAM_STAT_BUSY:
			result |= DID_BUS_BUSY << 16 | COMMAND_COMPLETE << 8;
			break;
		default:
			result |= DID_ERROR << 16 | COMMAND_COMPLETE << 8;
			break;
		}
	}
	else if (ccb->srb_status & SRB_SEE_SENSE)
		result = DRIVER_SENSE << 24 | SAM_STAT_CHECK_CONDITION;
	else switch (ccb->srb_status) {
		case SRB_STATUS_SELECTION_TIMEOUT:
			result = DID_NO_CONNECT << 16 | COMMAND_COMPLETE << 8;
			break;
		case SRB_STATUS_BUSY:
			result = DID_BUS_BUSY << 16 | COMMAND_COMPLETE << 8;
			break;
		case SRB_STATUS_INVALID_REQUEST:
		case SRB_STATUS_ERROR:
		default:
			result = DID_ERROR << 16 | COMMAND_COMPLETE << 8;
			break;
	}

	cmd->result = result;
	cmd->scsi_done(cmd);
}

static void stex_copy_data(struct st_ccb *ccb,
	struct status_msg *resp, unsigned int variable)
{
	size_t count = variable;
	if (resp->scsi_status != SAM_STAT_GOOD) {
		if (ccb->sense_buffer != NULL)
			memcpy(ccb->sense_buffer, resp->variable,
				min(variable, ccb->sense_bufflen));
		return;
	}

	if (ccb->cmd == NULL)
		return;
	stex_internal_copy(ccb->cmd,
		resp->variable, &count, ccb->sg_count, ST_TO_CMD);
}

static void stex_ys_commands(struct st_hba *hba,
	struct st_ccb *ccb, struct status_msg *resp)
{
	size_t count;

	if (ccb->cmd->cmnd[0] == MGT_CMD &&
		resp->scsi_status != SAM_STAT_CHECK_CONDITION) {
		ccb->cmd->request_bufflen =
			le32_to_cpu(*(__le32 *)&resp->variable[0]);
		return;
	}

	if (resp->srb_status != 0)
		return;

	/* determine inquiry command status by DeviceTypeQualifier */
	if (ccb->cmd->cmnd[0] == INQUIRY &&
		resp->scsi_status == SAM_STAT_GOOD) {
		ST_INQ *inq_data;

		count = STEX_EXTRA_SIZE;
		stex_internal_copy(ccb->cmd, hba->copy_buffer,
			&count, ccb->sg_count, ST_FROM_CMD);
		inq_data = (ST_INQ *)hba->copy_buffer;
		if (inq_data->DeviceTypeQualifier != 0)
			ccb->srb_status = SRB_STATUS_SELECTION_TIMEOUT;
		else
			ccb->srb_status = SRB_STATUS_SUCCESS;
	} else if (ccb->cmd->cmnd[0] == REPORT_LUNS) {
		u8 *report_lun_data = (u8 *)hba->copy_buffer;

		count = STEX_EXTRA_SIZE;
		stex_internal_copy(ccb->cmd, report_lun_data,
			&count, ccb->sg_count, ST_FROM_CMD);
		if (report_lun_data[2] || report_lun_data[3]) {
			report_lun_data[2] = 0x00;
			report_lun_data[3] = 0x08;
			stex_internal_copy(ccb->cmd, report_lun_data,
				&count, ccb->sg_count, ST_TO_CMD);
		}
	}
}

static void stex_mu_intr(struct st_hba *hba, u32 doorbell)
{
	void __iomem *base = hba->mmio_base;
	struct status_msg *resp;
	struct st_ccb *ccb;
	unsigned int size;
	u16 tag;

	if (!(doorbell & MU_OUTBOUND_DOORBELL_STATUSHEADCHANGED))
		return;

	/* status payloads */
	hba->status_head = readl(base + OMR1);
	if (unlikely(hba->status_head >= MU_STATUS_COUNT)) {
		printk(KERN_WARNING DRV_NAME "(%s): invalid status head\n",
			pci_name(hba->pdev));
		return;
	}

	/*
	 * it's not a valid status payload if:
	 * 1. there are no pending requests(e.g. during init stage)
	 * 2. there are some pending requests, but the controller is in
	 *     reset status, and its type is not st_yosemite
	 * firmware of st_yosemite in reset status will return pending requests
	 * to driver, so we allow it to pass
	 */
	if (unlikely(hba->out_req_cnt <= 0 ||
			(hba->mu_status == MU_STATE_RESETTING &&
			 hba->cardtype != st_yosemite))) {
		hba->status_tail = hba->status_head;
		goto update_status;
	}

	while (hba->status_tail != hba->status_head) {
		resp = stex_get_status(hba);
		tag = le16_to_cpu(resp->tag);
		if (unlikely(tag >= hba->host->can_queue)) {
			printk(KERN_WARNING DRV_NAME
				"(%s): invalid tag\n", pci_name(hba->pdev));
			continue;
		}

		ccb = &hba->ccb[tag];
		if (hba->wait_ccb == ccb)
			hba->wait_ccb = NULL;
		if (unlikely(ccb->req == NULL)) {
			printk(KERN_WARNING DRV_NAME
				"(%s): lagging req\n", pci_name(hba->pdev));
			hba->out_req_cnt--;
			continue;
		}

		size = resp->payload_sz * sizeof(u32); /* payload size */
		if (unlikely(size < sizeof(*resp) - STATUS_VAR_LEN ||
			size > sizeof(*resp))) {
			printk(KERN_WARNING DRV_NAME "(%s): bad status size\n",
				pci_name(hba->pdev));
		} else {
			size -= sizeof(*resp) - STATUS_VAR_LEN; /* copy size */
			if (size)
				stex_copy_data(ccb, resp, size);
		}

		ccb->srb_status = resp->srb_status;
		ccb->scsi_status = resp->scsi_status;

		if (likely(ccb->cmd != NULL)) {
			if (hba->cardtype == st_yosemite)
				stex_ys_commands(hba, ccb, resp);

			if (unlikely(ccb->cmd->cmnd[0] == PASSTHRU_CMD &&
				ccb->cmd->cmnd[1] == PASSTHRU_GET_ADAPTER))
				stex_controller_info(hba, ccb);

			stex_unmap_sg(hba, ccb->cmd);
			stex_scsi_done(ccb);
			hba->out_req_cnt--;
		} else if (ccb->req_type & PASSTHRU_REQ_TYPE) {
			hba->out_req_cnt--;
			if (ccb->req_type & PASSTHRU_REQ_NO_WAKEUP) {
				ccb->req_type = 0;
				continue;
			}
			ccb->req_type = 0;
			if (waitqueue_active(&hba->waitq))
				wake_up(&hba->waitq);
		}
	}

update_status:
	writel(hba->status_head, base + IMR1);
	readl(base + IMR1); /* flush */
}

static irqreturn_t stex_intr(int irq, void *__hba)
{
	struct st_hba *hba = __hba;
	void __iomem *base = hba->mmio_base;
	u32 data;
	unsigned long flags;
	int handled = 0;

	spin_lock_irqsave(hba->host->host_lock, flags);

	data = readl(base + ODBL);

	if (data && data != 0xffffffff) {
		/* clear the interrupt */
		writel(data, base + ODBL);
		readl(base + ODBL); /* flush */
		stex_mu_intr(hba, data);
		handled = 1;
	}

	spin_unlock_irqrestore(hba->host->host_lock, flags);

	return IRQ_RETVAL(handled);
}

static int stex_handshake(struct st_hba *hba)
{
	void __iomem *base = hba->mmio_base;
	struct handshake_frame *h;
	dma_addr_t status_phys;
	int i;

	if (readl(base + OMR0) != MU_HANDSHAKE_SIGNATURE) {
		writel(MU_INBOUND_DOORBELL_HANDSHAKE, base + IDBL);
		readl(base + IDBL);
		for (i = 0; readl(base + OMR0) != MU_HANDSHAKE_SIGNATURE
			&& i < MU_MAX_DELAY_TIME; i++) {
			rmb();
			msleep(1);
		}

		if (i == MU_MAX_DELAY_TIME) {
			printk(KERN_ERR DRV_NAME
				"(%s): no handshake signature\n",
				pci_name(hba->pdev));
			return -1;
		}
	}

	udelay(10);

	h = (struct handshake_frame *)(hba->dma_mem + MU_REQ_BUFFER_SIZE);
	h->rb_phy = cpu_to_le32(hba->dma_handle);
	h->rb_phy_hi = cpu_to_le32((hba->dma_handle >> 16) >> 16);
	h->req_sz = cpu_to_le16(sizeof(struct req_msg));
	h->req_cnt = cpu_to_le16(MU_REQ_COUNT);
	h->status_sz = cpu_to_le16(sizeof(struct status_msg));
	h->status_cnt = cpu_to_le16(MU_STATUS_COUNT);
	stex_gettime(&h->hosttime);
	h->partner_type = HMU_PARTNER_TYPE;

	status_phys = hba->dma_handle + MU_REQ_BUFFER_SIZE;
	writel(status_phys, base + IMR0);
	readl(base + IMR0);
	writel((status_phys >> 16) >> 16, base + IMR1);
	readl(base + IMR1);

	writel((status_phys >> 16) >> 16, base + OMR0); /* old fw compatible */
	readl(base + OMR0);
	writel(MU_INBOUND_DOORBELL_HANDSHAKE, base + IDBL);
	readl(base + IDBL); /* flush */

	udelay(10);
	for (i = 0; readl(base + OMR0) != MU_HANDSHAKE_SIGNATURE
		&& i < MU_MAX_DELAY_TIME; i++) {
		rmb();
		msleep(1);
	}

	if (i == MU_MAX_DELAY_TIME) {
		printk(KERN_ERR DRV_NAME
			"(%s): no signature after handshake frame\n",
			pci_name(hba->pdev));
		return -1;
	}

	writel(0, base + IMR0);
	readl(base + IMR0);
	writel(0, base + OMR0);
	readl(base + OMR0);
	writel(0, base + IMR1);
	readl(base + IMR1);
	writel(0, base + OMR1);
	readl(base + OMR1); /* flush */
	hba->mu_status = MU_STATE_STARTED;
	return 0;
}

static int stex_abort(struct scsi_cmnd *cmd)
{
	struct Scsi_Host *host = cmd->device->host;
	struct st_hba *hba = (struct st_hba *)host->hostdata;
	u16 tag = cmd->request->tag;
	void __iomem *base;
	u32 data;
	int result = SUCCESS;
	unsigned long flags;
	base = hba->mmio_base;
	spin_lock_irqsave(host->host_lock, flags);
	if (tag < host->can_queue && hba->ccb[tag].cmd == cmd)
		hba->wait_ccb = &hba->ccb[tag];
	else {
		for (tag = 0; tag < host->can_queue; tag++)
			if (hba->ccb[tag].cmd == cmd) {
				hba->wait_ccb = &hba->ccb[tag];
				break;
			}
		if (tag >= host->can_queue)
			goto out;
	}

	data = readl(base + ODBL);
	if (data == 0 || data == 0xffffffff)
		goto fail_out;

	writel(data, base + ODBL);
	readl(base + ODBL); /* flush */

	stex_mu_intr(hba, data);

	if (hba->wait_ccb == NULL) {
		printk(KERN_WARNING DRV_NAME
			"(%s): lost interrupt\n", pci_name(hba->pdev));
		goto out;
	}

fail_out:
	stex_unmap_sg(hba, cmd);
	hba->wait_ccb->req = NULL; /* nullify the req's future return */
	hba->wait_ccb = NULL;
	result = FAILED;
out:
	spin_unlock_irqrestore(host->host_lock, flags);
	return result;
}

static void stex_hard_reset(struct st_hba *hba)
{
	struct pci_bus *bus;
	int i;
	u16 pci_cmd;
	u8 pci_bctl;

	for (i = 0; i < 16; i++)
		pci_read_config_dword(hba->pdev, i * 4,
			&hba->pdev->saved_config_space[i]);

	/* Reset secondary bus. Our controller(MU/ATU) is the only device on
	   secondary bus. Consult Intel 80331/3 developer's manual for detail */
	bus = hba->pdev->bus;
	pci_read_config_byte(bus->self, PCI_BRIDGE_CONTROL, &pci_bctl);
	pci_bctl |= PCI_BRIDGE_CTL_BUS_RESET;
	pci_write_config_byte(bus->self, PCI_BRIDGE_CONTROL, pci_bctl);
	msleep(1);
	pci_bctl &= ~PCI_BRIDGE_CTL_BUS_RESET;
	pci_write_config_byte(bus->self, PCI_BRIDGE_CONTROL, pci_bctl);

	for (i = 0; i < MU_MAX_DELAY_TIME; i++) {
		pci_read_config_word(hba->pdev, PCI_COMMAND, &pci_cmd);
		if (pci_cmd & PCI_COMMAND_MASTER)
			break;
		msleep(1);
	}

	ssleep(5);
	for (i = 0; i < 16; i++)
		pci_write_config_dword(hba->pdev, i * 4,
			hba->pdev->saved_config_space[i]);
}

static int stex_reset(struct scsi_cmnd *cmd)
{
	struct st_hba *hba;
	unsigned long flags;
	unsigned long before;
	hba = (struct st_hba *) &cmd->device->host->hostdata[0];

	hba->mu_status = MU_STATE_RESETTING;

	if (hba->cardtype == st_shasta)
		stex_hard_reset(hba);

	if (hba->cardtype != st_yosemite) {
		if (stex_handshake(hba)) {
			printk(KERN_WARNING DRV_NAME
				"(%s): resetting: handshake failed\n",
				pci_name(hba->pdev));
			return FAILED;
		}
		spin_lock_irqsave(hba->host->host_lock, flags);
		hba->req_head = 0;
		hba->req_tail = 0;
		hba->status_head = 0;
		hba->status_tail = 0;
		hba->out_req_cnt = 0;
		spin_unlock_irqrestore(hba->host->host_lock, flags);
		return SUCCESS;
	}

	/* st_yosemite */
	writel(MU_INBOUND_DOORBELL_RESET, hba->mmio_base + IDBL);
	readl(hba->mmio_base + IDBL); /* flush */
	before = jiffies;
	while (hba->out_req_cnt > 0) {
		if (time_after(jiffies, before + ST_INTERNAL_TIMEOUT * HZ)) {
			printk(KERN_WARNING DRV_NAME
				"(%s): reset timeout\n", pci_name(hba->pdev));
			return FAILED;
		}
		msleep(1);
	}

	hba->mu_status = MU_STATE_STARTED;
	return SUCCESS;
}

static int stex_biosparam(struct scsi_device *sdev,
	struct block_device *bdev, sector_t capacity, int geom[])
{
	int heads = 255, sectors = 63;

	if (capacity < 0x200000) {
		heads = 64;
		sectors = 32;
	}

	sector_div(capacity, heads * sectors);

	geom[0] = heads;
	geom[1] = sectors;
	geom[2] = capacity;

	return 0;
}

static struct scsi_host_template driver_template = {
	.module				= THIS_MODULE,
	.name				= DRV_NAME,
	.proc_name			= DRV_NAME,
	.bios_param			= stex_biosparam,
	.queuecommand			= stex_queuecommand,
	.slave_alloc			= stex_slave_alloc,
	.slave_configure		= stex_slave_config,
	.slave_destroy			= stex_slave_destroy,
	.eh_abort_handler		= stex_abort,
	.eh_host_reset_handler		= stex_reset,
	.can_queue			= ST_CAN_QUEUE,
	.this_id			= -1,
	.sg_tablesize			= ST_MAX_SG,
	.cmd_per_lun			= ST_CMD_PER_LUN,
};

static int stex_set_dma_mask(struct pci_dev * pdev)
{
	int ret;
	if (!pci_set_dma_mask(pdev, DMA_64BIT_MASK)
		&& !pci_set_consistent_dma_mask(pdev, DMA_64BIT_MASK))
		return 0;
	ret = pci_set_dma_mask(pdev, DMA_32BIT_MASK);
	if (!ret)
		ret = pci_set_consistent_dma_mask(pdev, DMA_32BIT_MASK);
	return ret;
}

static int __devinit
stex_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct st_hba *hba;
	struct Scsi_Host *host;
	int err;

	err = pci_enable_device(pdev);
	if (err)
		return err;

	pci_set_master(pdev);

	host = scsi_host_alloc(&driver_template, sizeof(struct st_hba));

	if (!host) {
		printk(KERN_ERR DRV_NAME "(%s): scsi_host_alloc failed\n",
			pci_name(pdev));
		err = -ENOMEM;
		goto out_disable;
	}

	hba = (struct st_hba *)host->hostdata;
	memset(hba, 0, sizeof(struct st_hba));

	err = pci_request_regions(pdev, DRV_NAME);
	if (err < 0) {
		printk(KERN_ERR DRV_NAME "(%s): request regions failed\n",
			pci_name(pdev));
		goto out_scsi_host_put;
	}

	hba->mmio_base = ioremap(pci_resource_start(pdev, 0),
		pci_resource_len(pdev, 0));
	if ( !hba->mmio_base) {
		printk(KERN_ERR DRV_NAME "(%s): memory map failed\n",
			pci_name(pdev));
		err = -ENOMEM;
		goto out_release_regions;
	}

	err = stex_set_dma_mask(pdev);
	if (err) {
		printk(KERN_ERR DRV_NAME "(%s): set dma mask failed\n",
			pci_name(pdev));
		goto out_iounmap;
	}

	hba->dma_mem = dma_alloc_coherent(&pdev->dev,
		STEX_BUFFER_SIZE, &hba->dma_handle, GFP_KERNEL);
	if (!hba->dma_mem) {
		err = -ENOMEM;
		printk(KERN_ERR DRV_NAME "(%s): dma mem alloc failed\n",
			pci_name(pdev));
		goto out_iounmap;
	}

	hba->status_buffer =
		(struct status_msg *)(hba->dma_mem + MU_REQ_BUFFER_SIZE);
	hba->copy_buffer = hba->dma_mem + MU_BUFFER_SIZE;
	hba->mu_status = MU_STATE_STARTING;

	hba->cardtype = (unsigned int) id->driver_data;

	/* firmware uses id/lun pair for a logical drive, but lun would be
	   always 0 if CONFIG_SCSI_MULTI_LUN not configured, so we use
	   channel to map lun here */
	host->max_channel = ST_MAX_LUN_PER_TARGET - 1;
	host->max_id = ST_MAX_TARGET_NUM;
	host->max_lun = 1;
	host->unique_id = host->host_no;
	host->max_cmd_len = STEX_CDB_LENGTH;

	hba->host = host;
	hba->pdev = pdev;
	init_waitqueue_head(&hba->waitq);

	err = request_irq(pdev->irq, stex_intr, IRQF_SHARED, DRV_NAME, hba);
	if (err) {
		printk(KERN_ERR DRV_NAME "(%s): request irq failed\n",
			pci_name(pdev));
		goto out_pci_free;
	}

	err = stex_handshake(hba);
	if (err)
		goto out_free_irq;

	err = scsi_init_shared_tag_map(host, ST_CAN_QUEUE);
	if (err) {
		printk(KERN_ERR DRV_NAME "(%s): init shared queue failed\n",
			pci_name(pdev));
		goto out_free_irq;
	}

	pci_set_drvdata(pdev, hba);

	err = scsi_add_host(host, &pdev->dev);
	if (err) {
		printk(KERN_ERR DRV_NAME "(%s): scsi_add_host failed\n",
			pci_name(pdev));
		goto out_free_irq;
	}

	scsi_scan_host(host);

	return 0;

out_free_irq:
	free_irq(pdev->irq, hba);
out_pci_free:
	dma_free_coherent(&pdev->dev, STEX_BUFFER_SIZE,
			  hba->dma_mem, hba->dma_handle);
out_iounmap:
	iounmap(hba->mmio_base);
out_release_regions:
	pci_release_regions(pdev);
out_scsi_host_put:
	scsi_host_put(host);
out_disable:
	pci_disable_device(pdev);

	return err;
}

static void stex_hba_stop(struct st_hba *hba)
{
	struct req_msg *req;
	unsigned long flags;
	unsigned long before;
	u16 tag = 0;

	spin_lock_irqsave(hba->host->host_lock, flags);
	req = stex_alloc_req(hba);
	memset(req->cdb, 0, STEX_CDB_LENGTH);

	if (hba->cardtype == st_yosemite) {
		req->cdb[0] = MGT_CMD;
		req->cdb[1] = MGT_CMD_SIGNATURE;
		req->cdb[2] = CTLR_CONFIG_CMD;
		req->cdb[3] = CTLR_SHUTDOWN;
	} else {
		req->cdb[0] = CONTROLLER_CMD;
		req->cdb[1] = CTLR_POWER_STATE_CHANGE;
		req->cdb[2] = CTLR_POWER_SAVING;
	}

	hba->ccb[tag].cmd = NULL;
	hba->ccb[tag].sg_count = 0;
	hba->ccb[tag].sense_bufflen = 0;
	hba->ccb[tag].sense_buffer = NULL;
	hba->ccb[tag].req_type |= PASSTHRU_REQ_TYPE;

	stex_send_cmd(hba, req, tag);
	spin_unlock_irqrestore(hba->host->host_lock, flags);

	before = jiffies;
	while (hba->ccb[tag].req_type & PASSTHRU_REQ_TYPE) {
		if (time_after(jiffies, before + ST_INTERNAL_TIMEOUT * HZ))
			return;
		msleep(10);
	}
}

static void stex_hba_free(struct st_hba *hba)
{
	free_irq(hba->pdev->irq, hba);

	iounmap(hba->mmio_base);

	pci_release_regions(hba->pdev);

	dma_free_coherent(&hba->pdev->dev, STEX_BUFFER_SIZE,
			  hba->dma_mem, hba->dma_handle);
}

static void stex_remove(struct pci_dev *pdev)
{
	struct st_hba *hba = pci_get_drvdata(pdev);

	scsi_remove_host(hba->host);

	pci_set_drvdata(pdev, NULL);

	stex_hba_stop(hba);

	stex_hba_free(hba);

	scsi_host_put(hba->host);

	pci_disable_device(pdev);
}

static void stex_shutdown(struct pci_dev *pdev)
{
	struct st_hba *hba = pci_get_drvdata(pdev);

	stex_hba_stop(hba);
}

static struct pci_device_id stex_pci_tbl[] = {
	{ 0x105a, 0x8350, PCI_ANY_ID, PCI_ANY_ID, 0, 0, st_shasta },
	{ 0x105a, 0xc350, PCI_ANY_ID, PCI_ANY_ID, 0, 0, st_shasta },
	{ 0x105a, 0xf350, PCI_ANY_ID, PCI_ANY_ID, 0, 0, st_shasta },
	{ 0x105a, 0x4301, PCI_ANY_ID, PCI_ANY_ID, 0, 0, st_shasta },
	{ 0x105a, 0x4302, PCI_ANY_ID, PCI_ANY_ID, 0, 0, st_shasta },
	{ 0x105a, 0x8301, PCI_ANY_ID, PCI_ANY_ID, 0, 0, st_shasta },
	{ 0x105a, 0x8302, PCI_ANY_ID, PCI_ANY_ID, 0, 0, st_shasta },
	{ 0x1725, 0x7250, PCI_ANY_ID, PCI_ANY_ID, 0, 0, st_vsc },
	{ 0x105a, 0x8650, PCI_ANY_ID, PCI_ANY_ID, 0, 0, st_yosemite },
	{ }	/* terminate list */
};
MODULE_DEVICE_TABLE(pci, stex_pci_tbl);

static struct pci_driver stex_pci_driver = {
	.name		= DRV_NAME,
	.id_table	= stex_pci_tbl,
	.probe		= stex_probe,
	.remove		= __devexit_p(stex_remove),
	.shutdown	= stex_shutdown,
};

static int __init stex_init(void)
{
	printk(KERN_INFO DRV_NAME
		": Promise SuperTrak EX Driver version: %s\n",
		 ST_DRIVER_VERSION);

	return pci_register_driver(&stex_pci_driver);
}

static void __exit stex_exit(void)
{
	pci_unregister_driver(&stex_pci_driver);
}

module_init(stex_init);
module_exit(stex_exit);
