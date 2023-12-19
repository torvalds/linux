// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * SuperTrak EX Series Storage Controller driver for Linux
 *
 *	Copyright (C) 2005-2015 Promise Technology Inc.
 *
 *	Written By:
 *		Ed Lin <promise_linux@promise.com>
 */

#include <linux/init.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/pci.h>
#include <linux/blkdev.h>
#include <linux/interrupt.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/ktime.h>
#include <linux/reboot.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/byteorder.h>
#include <scsi/scsi.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsi_dbg.h>
#include <scsi/scsi_eh.h>

#define DRV_NAME "stex"
#define ST_DRIVER_VERSION	"6.02.0000.01"
#define ST_VER_MAJOR		6
#define ST_VER_MINOR		02
#define ST_OEM				0000
#define ST_BUILD_VER		01

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

	YIOA_STATUS				= 0x00,
	YH2I_INT				= 0x20,
	YINT_EN					= 0x34,
	YI2H_INT				= 0x9c,
	YI2H_INT_C				= 0xa0,
	YH2I_REQ				= 0xc0,
	YH2I_REQ_HI				= 0xc4,
	PSCRATCH0				= 0xb0,
	PSCRATCH1				= 0xb4,
	PSCRATCH2				= 0xb8,
	PSCRATCH3				= 0xbc,
	PSCRATCH4				= 0xc8,
	MAILBOX_BASE			= 0x1000,
	MAILBOX_HNDSHK_STS		= 0x0,

	/* MU register value */
	MU_INBOUND_DOORBELL_HANDSHAKE		= (1 << 0),
	MU_INBOUND_DOORBELL_REQHEADCHANGED	= (1 << 1),
	MU_INBOUND_DOORBELL_STATUSTAILCHANGED	= (1 << 2),
	MU_INBOUND_DOORBELL_HMUSTOPPED		= (1 << 3),
	MU_INBOUND_DOORBELL_RESET		= (1 << 4),

	MU_OUTBOUND_DOORBELL_HANDSHAKE		= (1 << 0),
	MU_OUTBOUND_DOORBELL_REQUESTTAILCHANGED	= (1 << 1),
	MU_OUTBOUND_DOORBELL_STATUSHEADCHANGED	= (1 << 2),
	MU_OUTBOUND_DOORBELL_BUSCHANGE		= (1 << 3),
	MU_OUTBOUND_DOORBELL_HASEVENT		= (1 << 4),
	MU_OUTBOUND_DOORBELL_REQUEST_RESET	= (1 << 27),

	/* MU status code */
	MU_STATE_STARTING			= 1,
	MU_STATE_STARTED			= 2,
	MU_STATE_RESETTING			= 3,
	MU_STATE_FAILED				= 4,
	MU_STATE_STOP				= 5,
	MU_STATE_NOCONNECT			= 6,

	MU_MAX_DELAY				= 50,
	MU_HANDSHAKE_SIGNATURE			= 0x55aaaa55,
	MU_HANDSHAKE_SIGNATURE_HALF		= 0x5a5a0000,
	MU_HARD_RESET_WAIT			= 30000,
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
};

enum {
	SS_STS_NORMAL				= 0x80000000,
	SS_STS_DONE				= 0x40000000,
	SS_STS_HANDSHAKE			= 0x20000000,

	SS_HEAD_HANDSHAKE			= 0x80,

	SS_H2I_INT_RESET			= 0x100,

	SS_I2H_REQUEST_RESET			= 0x2000,

	SS_MU_OPERATIONAL			= 0x80000000,
};

enum {
	STEX_CDB_LENGTH				= 16,
	STATUS_VAR_LEN				= 128,

	/* sg flags */
	SG_CF_EOT				= 0x80,	/* end of table */
	SG_CF_64B				= 0x40,	/* 64 bit item */
	SG_CF_HOST				= 0x20,	/* sg in host memory */
	MSG_DATA_DIR_ND				= 0,
	MSG_DATA_DIR_IN				= 1,
	MSG_DATA_DIR_OUT			= 2,

	st_shasta				= 0,
	st_vsc					= 1,
	st_yosemite				= 2,
	st_seq					= 3,
	st_yel					= 4,
	st_P3					= 5,

	PASSTHRU_REQ_TYPE			= 0x00000001,
	PASSTHRU_REQ_NO_WAKEUP			= 0x00000100,
	ST_INTERNAL_TIMEOUT			= 180,

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

	ST_ADDITIONAL_MEM			= 0x200000,
	ST_ADDITIONAL_MEM_MIN			= 0x80000,
	PMIC_SHUTDOWN				= 0x0D,
	PMIC_REUMSE					= 0x10,
	ST_IGNORED					= -1,
	ST_NOTHANDLED				= 7,
	ST_S3						= 3,
	ST_S4						= 4,
	ST_S5						= 5,
	ST_S6						= 6,
};

struct st_sgitem {
	u8 ctrl;	/* SG_CF_xxx */
	u8 reserved[3];
	__le32 count;
	__le64 addr;
};

struct st_ss_sgitem {
	__le32 addr;
	__le32 addr_hi;
	__le32 count;
};

struct st_sgtable {
	__le16 sg_count;
	__le16 max_sg_count;
	__le32 sz_in_byte;
};

struct st_msg_header {
	__le64 handle;
	u8 flag;
	u8 channel;
	__le16 timeout;
	u32 reserved;
};

struct handshake_frame {
	__le64 rb_phy;		/* request payload queue physical address */
	__le16 req_sz;		/* size of each request payload */
	__le16 req_cnt;		/* count of reqs the buffer can hold */
	__le16 status_sz;	/* size of each status payload */
	__le16 status_cnt;	/* count of status the buffer can hold */
	__le64 hosttime;	/* seconds from Jan 1, 1970 (GMT) */
	u8 partner_type;	/* who sends this frame */
	u8 reserved0[7];
	__le32 partner_ver_major;
	__le32 partner_ver_minor;
	__le32 partner_ver_oem;
	__le32 partner_ver_build;
	__le32 extra_offset;	/* NEW */
	__le32 extra_size;	/* NEW */
	__le32 scratch_size;
	u32 reserved1;
};

struct req_msg {
	__le16 tag;
	u8 lun;
	u8 target;
	u8 task_attr;
	u8 task_manage;
	u8 data_dir;
	u8 payload_sz;		/* payload size in 4-byte, not used */
	u8 cdb[STEX_CDB_LENGTH];
	u32 variable[];
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

struct st_ccb {
	struct req_msg *req;
	struct scsi_cmnd *cmd;

	void *sense_buffer;
	unsigned int sense_bufflen;
	int sg_count;

	u32 req_type;
	u8 srb_status;
	u8 scsi_status;
	u8 reserved[2];
};

struct st_hba {
	void __iomem *mmio_base;	/* iomapped PCI memory space */
	void *dma_mem;
	dma_addr_t dma_handle;
	size_t dma_size;

	struct Scsi_Host *host;
	struct pci_dev *pdev;

	struct req_msg * (*alloc_rq) (struct st_hba *);
	int (*map_sg)(struct st_hba *, struct req_msg *, struct st_ccb *);
	void (*send) (struct st_hba *, struct req_msg *, u16);

	u32 req_head;
	u32 req_tail;
	u32 status_head;
	u32 status_tail;

	struct status_msg *status_buffer;
	void *copy_buffer; /* temp buffer for driver-handled commands */
	struct st_ccb *ccb;
	struct st_ccb *wait_ccb;
	__le32 *scratch;

	char work_q_name[20];
	struct workqueue_struct *work_q;
	struct work_struct reset_work;
	wait_queue_head_t reset_waitq;
	unsigned int mu_status;
	unsigned int cardtype;
	int msi_enabled;
	int out_req_cnt;
	u32 extra_offset;
	u16 rq_count;
	u16 rq_size;
	u16 sts_count;
	u8  supports_pm;
	int msi_lock;
};

struct st_card_info {
	struct req_msg * (*alloc_rq) (struct st_hba *);
	int (*map_sg)(struct st_hba *, struct req_msg *, struct st_ccb *);
	void (*send) (struct st_hba *, struct req_msg *, u16);
	unsigned int max_id;
	unsigned int max_lun;
	unsigned int max_channel;
	u16 rq_count;
	u16 rq_size;
	u16 sts_count;
};

static int S6flag;
static int stex_halt(struct notifier_block *nb, ulong event, void *buf);
static struct notifier_block stex_notifier = {
	stex_halt, NULL, 0
};

static int msi;
module_param(msi, int, 0);
MODULE_PARM_DESC(msi, "Enable Message Signaled Interrupts(0=off, 1=on)");

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

static struct status_msg *stex_get_status(struct st_hba *hba)
{
	struct status_msg *status = hba->status_buffer + hba->status_tail;

	++hba->status_tail;
	hba->status_tail %= hba->sts_count+1;

	return status;
}

static void stex_invalid_field(struct scsi_cmnd *cmd,
			       void (*done)(struct scsi_cmnd *))
{
	/* "Invalid field in cdb" */
	scsi_build_sense(cmd, 0, ILLEGAL_REQUEST, 0x24, 0x0);
	done(cmd);
}

static struct req_msg *stex_alloc_req(struct st_hba *hba)
{
	struct req_msg *req = hba->dma_mem + hba->req_head * hba->rq_size;

	++hba->req_head;
	hba->req_head %= hba->rq_count+1;

	return req;
}

static struct req_msg *stex_ss_alloc_req(struct st_hba *hba)
{
	return (struct req_msg *)(hba->dma_mem +
		hba->req_head * hba->rq_size + sizeof(struct st_msg_header));
}

static int stex_map_sg(struct st_hba *hba,
	struct req_msg *req, struct st_ccb *ccb)
{
	struct scsi_cmnd *cmd;
	struct scatterlist *sg;
	struct st_sgtable *dst;
	struct st_sgitem *table;
	int i, nseg;

	cmd = ccb->cmd;
	nseg = scsi_dma_map(cmd);
	BUG_ON(nseg < 0);
	if (nseg) {
		dst = (struct st_sgtable *)req->variable;

		ccb->sg_count = nseg;
		dst->sg_count = cpu_to_le16((u16)nseg);
		dst->max_sg_count = cpu_to_le16(hba->host->sg_tablesize);
		dst->sz_in_byte = cpu_to_le32(scsi_bufflen(cmd));

		table = (struct st_sgitem *)(dst + 1);
		scsi_for_each_sg(cmd, sg, nseg, i) {
			table[i].count = cpu_to_le32((u32)sg_dma_len(sg));
			table[i].addr = cpu_to_le64(sg_dma_address(sg));
			table[i].ctrl = SG_CF_64B | SG_CF_HOST;
		}
		table[--i].ctrl |= SG_CF_EOT;
	}

	return nseg;
}

static int stex_ss_map_sg(struct st_hba *hba,
	struct req_msg *req, struct st_ccb *ccb)
{
	struct scsi_cmnd *cmd;
	struct scatterlist *sg;
	struct st_sgtable *dst;
	struct st_ss_sgitem *table;
	int i, nseg;

	cmd = ccb->cmd;
	nseg = scsi_dma_map(cmd);
	BUG_ON(nseg < 0);
	if (nseg) {
		dst = (struct st_sgtable *)req->variable;

		ccb->sg_count = nseg;
		dst->sg_count = cpu_to_le16((u16)nseg);
		dst->max_sg_count = cpu_to_le16(hba->host->sg_tablesize);
		dst->sz_in_byte = cpu_to_le32(scsi_bufflen(cmd));

		table = (struct st_ss_sgitem *)(dst + 1);
		scsi_for_each_sg(cmd, sg, nseg, i) {
			table[i].count = cpu_to_le32((u32)sg_dma_len(sg));
			table[i].addr =
				cpu_to_le32(sg_dma_address(sg) & 0xffffffff);
			table[i].addr_hi =
				cpu_to_le32((sg_dma_address(sg) >> 16) >> 16);
		}
	}

	return nseg;
}

static void stex_controller_info(struct st_hba *hba, struct st_ccb *ccb)
{
	struct st_frame *p;
	size_t count = sizeof(struct st_frame);

	p = hba->copy_buffer;
	scsi_sg_copy_to_buffer(ccb->cmd, p, count);
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

	scsi_sg_copy_from_buffer(ccb->cmd, p, count);
}

static void
stex_send_cmd(struct st_hba *hba, struct req_msg *req, u16 tag)
{
	req->tag = cpu_to_le16(tag);

	hba->ccb[tag].req = req;
	hba->out_req_cnt++;

	writel(hba->req_head, hba->mmio_base + IMR0);
	writel(MU_INBOUND_DOORBELL_REQHEADCHANGED, hba->mmio_base + IDBL);
	readl(hba->mmio_base + IDBL); /* flush */
}

static void
stex_ss_send_cmd(struct st_hba *hba, struct req_msg *req, u16 tag)
{
	struct scsi_cmnd *cmd;
	struct st_msg_header *msg_h;
	dma_addr_t addr;

	req->tag = cpu_to_le16(tag);

	hba->ccb[tag].req = req;
	hba->out_req_cnt++;

	cmd = hba->ccb[tag].cmd;
	msg_h = (struct st_msg_header *)req - 1;
	if (likely(cmd)) {
		msg_h->channel = (u8)cmd->device->channel;
		msg_h->timeout = cpu_to_le16(scsi_cmd_to_rq(cmd)->timeout / HZ);
	}
	addr = hba->dma_handle + hba->req_head * hba->rq_size;
	addr += (hba->ccb[tag].sg_count+4)/11;
	msg_h->handle = cpu_to_le64(addr);

	++hba->req_head;
	hba->req_head %= hba->rq_count+1;
	if (hba->cardtype == st_P3) {
		writel((addr >> 16) >> 16, hba->mmio_base + YH2I_REQ_HI);
		writel(addr, hba->mmio_base + YH2I_REQ);
	} else {
		writel((addr >> 16) >> 16, hba->mmio_base + YH2I_REQ_HI);
		readl(hba->mmio_base + YH2I_REQ_HI); /* flush */
		writel(addr, hba->mmio_base + YH2I_REQ);
		readl(hba->mmio_base + YH2I_REQ); /* flush */
	}
}

static void return_abnormal_state(struct st_hba *hba, int status)
{
	struct st_ccb *ccb;
	unsigned long flags;
	u16 tag;

	spin_lock_irqsave(hba->host->host_lock, flags);
	for (tag = 0; tag < hba->host->can_queue; tag++) {
		ccb = &hba->ccb[tag];
		if (ccb->req == NULL)
			continue;
		ccb->req = NULL;
		if (ccb->cmd) {
			scsi_dma_unmap(ccb->cmd);
			ccb->cmd->result = status << 16;
			scsi_done(ccb->cmd);
			ccb->cmd = NULL;
		}
	}
	spin_unlock_irqrestore(hba->host->host_lock, flags);
}
static int
stex_slave_config(struct scsi_device *sdev)
{
	sdev->use_10_for_rw = 1;
	sdev->use_10_for_ms = 1;
	blk_queue_rq_timeout(sdev->request_queue, 60 * HZ);

	return 0;
}

static int stex_queuecommand_lck(struct scsi_cmnd *cmd)
{
	void (*done)(struct scsi_cmnd *) = scsi_done;
	struct st_hba *hba;
	struct Scsi_Host *host;
	unsigned int id, lun;
	struct req_msg *req;
	u16 tag;

	host = cmd->device->host;
	id = cmd->device->id;
	lun = cmd->device->lun;
	hba = (struct st_hba *) &host->hostdata[0];
	if (hba->mu_status == MU_STATE_NOCONNECT) {
		cmd->result = DID_NO_CONNECT;
		done(cmd);
		return 0;
	}
	if (unlikely(hba->mu_status != MU_STATE_STARTED))
		return SCSI_MLQUEUE_HOST_BUSY;

	switch (cmd->cmnd[0]) {
	case MODE_SENSE_10:
	{
		static char ms10_caching_page[12] =
			{ 0, 0x12, 0, 0, 0, 0, 0, 0, 0x8, 0xa, 0x4, 0 };
		unsigned char page;

		page = cmd->cmnd[2] & 0x3f;
		if (page == 0x8 || page == 0x3f) {
			scsi_sg_copy_from_buffer(cmd, ms10_caching_page,
						 sizeof(ms10_caching_page));
			cmd->result = DID_OK << 16;
			done(cmd);
		} else
			stex_invalid_field(cmd, done);
		return 0;
	}
	case REPORT_LUNS:
		/*
		 * The shasta firmware does not report actual luns in the
		 * target, so fail the command to force sequential lun scan.
		 * Also, the console device does not support this command.
		 */
		if (hba->cardtype == st_shasta || id == host->max_id - 1) {
			stex_invalid_field(cmd, done);
			return 0;
		}
		break;
	case TEST_UNIT_READY:
		if (id == host->max_id - 1) {
			cmd->result = DID_OK << 16;
			done(cmd);
			return 0;
		}
		break;
	case INQUIRY:
		if (lun >= host->max_lun) {
			cmd->result = DID_NO_CONNECT << 16;
			done(cmd);
			return 0;
		}
		if (id != host->max_id - 1)
			break;
		if (!lun && !cmd->device->channel &&
			(cmd->cmnd[1] & INQUIRY_EVPD) == 0) {
			scsi_sg_copy_from_buffer(cmd, (void *)console_inq_page,
						 sizeof(console_inq_page));
			cmd->result = DID_OK << 16;
			done(cmd);
		} else
			stex_invalid_field(cmd, done);
		return 0;
	case PASSTHRU_CMD:
		if (cmd->cmnd[1] == PASSTHRU_GET_DRVVER) {
			const struct st_drvver ver = {
				.major = ST_VER_MAJOR,
				.minor = ST_VER_MINOR,
				.oem = ST_OEM,
				.build = ST_BUILD_VER,
				.signature[0] = PASSTHRU_SIGNATURE,
				.console_id = host->max_id - 1,
				.host_no = hba->host->host_no,
			};
			size_t cp_len = sizeof(ver);

			cp_len = scsi_sg_copy_from_buffer(cmd, &ver, cp_len);
			if (sizeof(ver) == cp_len)
				cmd->result = DID_OK << 16;
			else
				cmd->result = DID_ERROR << 16;
			done(cmd);
			return 0;
		}
		break;
	default:
		break;
	}

	tag = scsi_cmd_to_rq(cmd)->tag;

	if (unlikely(tag >= host->can_queue))
		return SCSI_MLQUEUE_HOST_BUSY;

	req = hba->alloc_rq(hba);

	req->lun = lun;
	req->target = id;

	/* cdb */
	memcpy(req->cdb, cmd->cmnd, STEX_CDB_LENGTH);

	if (cmd->sc_data_direction == DMA_FROM_DEVICE)
		req->data_dir = MSG_DATA_DIR_IN;
	else if (cmd->sc_data_direction == DMA_TO_DEVICE)
		req->data_dir = MSG_DATA_DIR_OUT;
	else
		req->data_dir = MSG_DATA_DIR_ND;

	hba->ccb[tag].cmd = cmd;
	hba->ccb[tag].sense_bufflen = SCSI_SENSE_BUFFERSIZE;
	hba->ccb[tag].sense_buffer = cmd->sense_buffer;

	if (!hba->map_sg(hba, req, &hba->ccb[tag])) {
		hba->ccb[tag].sg_count = 0;
		memset(&req->variable[0], 0, 8);
	}

	hba->send(hba, req, tag);
	return 0;
}

static DEF_SCSI_QCMD(stex_queuecommand)

static void stex_scsi_done(struct st_ccb *ccb)
{
	struct scsi_cmnd *cmd = ccb->cmd;
	int result;

	if (ccb->srb_status == SRB_STATUS_SUCCESS || ccb->srb_status == 0) {
		result = ccb->scsi_status;
		switch (ccb->scsi_status) {
		case SAM_STAT_GOOD:
			result |= DID_OK << 16;
			break;
		case SAM_STAT_CHECK_CONDITION:
			result |= DID_OK << 16;
			break;
		case SAM_STAT_BUSY:
			result |= DID_BUS_BUSY << 16;
			break;
		default:
			result |= DID_ERROR << 16;
			break;
		}
	}
	else if (ccb->srb_status & SRB_SEE_SENSE)
		result = SAM_STAT_CHECK_CONDITION;
	else switch (ccb->srb_status) {
		case SRB_STATUS_SELECTION_TIMEOUT:
			result = DID_NO_CONNECT << 16;
			break;
		case SRB_STATUS_BUSY:
			result = DID_BUS_BUSY << 16;
			break;
		case SRB_STATUS_INVALID_REQUEST:
		case SRB_STATUS_ERROR:
		default:
			result = DID_ERROR << 16;
			break;
	}

	cmd->result = result;
	scsi_done(cmd);
}

static void stex_copy_data(struct st_ccb *ccb,
	struct status_msg *resp, unsigned int variable)
{
	if (resp->scsi_status != SAM_STAT_GOOD) {
		if (ccb->sense_buffer != NULL)
			memcpy(ccb->sense_buffer, resp->variable,
				min(variable, ccb->sense_bufflen));
		return;
	}

	if (ccb->cmd == NULL)
		return;
	scsi_sg_copy_from_buffer(ccb->cmd, resp->variable, variable);
}

static void stex_check_cmd(struct st_hba *hba,
	struct st_ccb *ccb, struct status_msg *resp)
{
	if (ccb->cmd->cmnd[0] == MGT_CMD &&
		resp->scsi_status != SAM_STAT_CHECK_CONDITION)
		scsi_set_resid(ccb->cmd, scsi_bufflen(ccb->cmd) -
			le32_to_cpu(*(__le32 *)&resp->variable[0]));
}

static void stex_mu_intr(struct st_hba *hba, u32 doorbell)
{
	void __iomem *base = hba->mmio_base;
	struct status_msg *resp;
	struct st_ccb *ccb;
	unsigned int size;
	u16 tag;

	if (unlikely(!(doorbell & MU_OUTBOUND_DOORBELL_STATUSHEADCHANGED)))
		return;

	/* status payloads */
	hba->status_head = readl(base + OMR1);
	if (unlikely(hba->status_head > hba->sts_count)) {
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

		hba->out_req_cnt--;
		ccb = &hba->ccb[tag];
		if (unlikely(hba->wait_ccb == ccb))
			hba->wait_ccb = NULL;
		if (unlikely(ccb->req == NULL)) {
			printk(KERN_WARNING DRV_NAME
				"(%s): lagging req\n", pci_name(hba->pdev));
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

		ccb->req = NULL;
		ccb->srb_status = resp->srb_status;
		ccb->scsi_status = resp->scsi_status;

		if (likely(ccb->cmd != NULL)) {
			if (hba->cardtype == st_yosemite)
				stex_check_cmd(hba, ccb, resp);

			if (unlikely(ccb->cmd->cmnd[0] == PASSTHRU_CMD &&
				ccb->cmd->cmnd[1] == PASSTHRU_GET_ADAPTER))
				stex_controller_info(hba, ccb);

			scsi_dma_unmap(ccb->cmd);
			stex_scsi_done(ccb);
		} else
			ccb->req_type = 0;
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

	spin_lock_irqsave(hba->host->host_lock, flags);

	data = readl(base + ODBL);

	if (data && data != 0xffffffff) {
		/* clear the interrupt */
		writel(data, base + ODBL);
		readl(base + ODBL); /* flush */
		stex_mu_intr(hba, data);
		spin_unlock_irqrestore(hba->host->host_lock, flags);
		if (unlikely(data & MU_OUTBOUND_DOORBELL_REQUEST_RESET &&
			hba->cardtype == st_shasta))
			queue_work(hba->work_q, &hba->reset_work);
		return IRQ_HANDLED;
	}

	spin_unlock_irqrestore(hba->host->host_lock, flags);

	return IRQ_NONE;
}

static void stex_ss_mu_intr(struct st_hba *hba)
{
	struct status_msg *resp;
	struct st_ccb *ccb;
	__le32 *scratch;
	unsigned int size;
	int count = 0;
	u32 value;
	u16 tag;

	if (unlikely(hba->out_req_cnt <= 0 ||
			hba->mu_status == MU_STATE_RESETTING))
		return;

	while (count < hba->sts_count) {
		scratch = hba->scratch + hba->status_tail;
		value = le32_to_cpu(*scratch);
		if (unlikely(!(value & SS_STS_NORMAL)))
			return;

		resp = hba->status_buffer + hba->status_tail;
		*scratch = 0;
		++count;
		++hba->status_tail;
		hba->status_tail %= hba->sts_count+1;

		tag = (u16)value;
		if (unlikely(tag >= hba->host->can_queue)) {
			printk(KERN_WARNING DRV_NAME
				"(%s): invalid tag\n", pci_name(hba->pdev));
			continue;
		}

		hba->out_req_cnt--;
		ccb = &hba->ccb[tag];
		if (unlikely(hba->wait_ccb == ccb))
			hba->wait_ccb = NULL;
		if (unlikely(ccb->req == NULL)) {
			printk(KERN_WARNING DRV_NAME
				"(%s): lagging req\n", pci_name(hba->pdev));
			continue;
		}

		ccb->req = NULL;
		if (likely(value & SS_STS_DONE)) { /* normal case */
			ccb->srb_status = SRB_STATUS_SUCCESS;
			ccb->scsi_status = SAM_STAT_GOOD;
		} else {
			ccb->srb_status = resp->srb_status;
			ccb->scsi_status = resp->scsi_status;
			size = resp->payload_sz * sizeof(u32);
			if (unlikely(size < sizeof(*resp) - STATUS_VAR_LEN ||
				size > sizeof(*resp))) {
				printk(KERN_WARNING DRV_NAME
					"(%s): bad status size\n",
					pci_name(hba->pdev));
			} else {
				size -= sizeof(*resp) - STATUS_VAR_LEN;
				if (size)
					stex_copy_data(ccb, resp, size);
			}
			if (likely(ccb->cmd != NULL))
				stex_check_cmd(hba, ccb, resp);
		}

		if (likely(ccb->cmd != NULL)) {
			scsi_dma_unmap(ccb->cmd);
			stex_scsi_done(ccb);
		} else
			ccb->req_type = 0;
	}
}

static irqreturn_t stex_ss_intr(int irq, void *__hba)
{
	struct st_hba *hba = __hba;
	void __iomem *base = hba->mmio_base;
	u32 data;
	unsigned long flags;

	spin_lock_irqsave(hba->host->host_lock, flags);

	if (hba->cardtype == st_yel) {
		data = readl(base + YI2H_INT);
		if (data && data != 0xffffffff) {
			/* clear the interrupt */
			writel(data, base + YI2H_INT_C);
			stex_ss_mu_intr(hba);
			spin_unlock_irqrestore(hba->host->host_lock, flags);
			if (unlikely(data & SS_I2H_REQUEST_RESET))
				queue_work(hba->work_q, &hba->reset_work);
			return IRQ_HANDLED;
		}
	} else {
		data = readl(base + PSCRATCH4);
		if (data != 0xffffffff) {
			if (data != 0) {
				/* clear the interrupt */
				writel(data, base + PSCRATCH1);
				writel((1 << 22), base + YH2I_INT);
			}
			stex_ss_mu_intr(hba);
			spin_unlock_irqrestore(hba->host->host_lock, flags);
			if (unlikely(data & SS_I2H_REQUEST_RESET))
				queue_work(hba->work_q, &hba->reset_work);
			return IRQ_HANDLED;
		}
	}

	spin_unlock_irqrestore(hba->host->host_lock, flags);

	return IRQ_NONE;
}

static int stex_common_handshake(struct st_hba *hba)
{
	void __iomem *base = hba->mmio_base;
	struct handshake_frame *h;
	dma_addr_t status_phys;
	u32 data;
	unsigned long before;

	if (readl(base + OMR0) != MU_HANDSHAKE_SIGNATURE) {
		writel(MU_INBOUND_DOORBELL_HANDSHAKE, base + IDBL);
		readl(base + IDBL);
		before = jiffies;
		while (readl(base + OMR0) != MU_HANDSHAKE_SIGNATURE) {
			if (time_after(jiffies, before + MU_MAX_DELAY * HZ)) {
				printk(KERN_ERR DRV_NAME
					"(%s): no handshake signature\n",
					pci_name(hba->pdev));
				return -1;
			}
			rmb();
			msleep(1);
		}
	}

	udelay(10);

	data = readl(base + OMR1);
	if ((data & 0xffff0000) == MU_HANDSHAKE_SIGNATURE_HALF) {
		data &= 0x0000ffff;
		if (hba->host->can_queue > data) {
			hba->host->can_queue = data;
			hba->host->cmd_per_lun = data;
		}
	}

	h = (struct handshake_frame *)hba->status_buffer;
	h->rb_phy = cpu_to_le64(hba->dma_handle);
	h->req_sz = cpu_to_le16(hba->rq_size);
	h->req_cnt = cpu_to_le16(hba->rq_count+1);
	h->status_sz = cpu_to_le16(sizeof(struct status_msg));
	h->status_cnt = cpu_to_le16(hba->sts_count+1);
	h->hosttime = cpu_to_le64(ktime_get_real_seconds());
	h->partner_type = HMU_PARTNER_TYPE;
	if (hba->extra_offset) {
		h->extra_offset = cpu_to_le32(hba->extra_offset);
		h->extra_size = cpu_to_le32(hba->dma_size - hba->extra_offset);
	} else
		h->extra_offset = h->extra_size = 0;

	status_phys = hba->dma_handle + (hba->rq_count+1) * hba->rq_size;
	writel(status_phys, base + IMR0);
	readl(base + IMR0);
	writel((status_phys >> 16) >> 16, base + IMR1);
	readl(base + IMR1);

	writel((status_phys >> 16) >> 16, base + OMR0); /* old fw compatible */
	readl(base + OMR0);
	writel(MU_INBOUND_DOORBELL_HANDSHAKE, base + IDBL);
	readl(base + IDBL); /* flush */

	udelay(10);
	before = jiffies;
	while (readl(base + OMR0) != MU_HANDSHAKE_SIGNATURE) {
		if (time_after(jiffies, before + MU_MAX_DELAY * HZ)) {
			printk(KERN_ERR DRV_NAME
				"(%s): no signature after handshake frame\n",
				pci_name(hba->pdev));
			return -1;
		}
		rmb();
		msleep(1);
	}

	writel(0, base + IMR0);
	readl(base + IMR0);
	writel(0, base + OMR0);
	readl(base + OMR0);
	writel(0, base + IMR1);
	readl(base + IMR1);
	writel(0, base + OMR1);
	readl(base + OMR1); /* flush */
	return 0;
}

static int stex_ss_handshake(struct st_hba *hba)
{
	void __iomem *base = hba->mmio_base;
	struct st_msg_header *msg_h;
	struct handshake_frame *h;
	__le32 *scratch;
	u32 data, scratch_size, mailboxdata, operationaldata;
	unsigned long before;
	int ret = 0;

	before = jiffies;

	if (hba->cardtype == st_yel) {
		operationaldata = readl(base + YIOA_STATUS);
		while (operationaldata != SS_MU_OPERATIONAL) {
			if (time_after(jiffies, before + MU_MAX_DELAY * HZ)) {
				printk(KERN_ERR DRV_NAME
					"(%s): firmware not operational\n",
					pci_name(hba->pdev));
				return -1;
			}
			msleep(1);
			operationaldata = readl(base + YIOA_STATUS);
		}
	} else {
		operationaldata = readl(base + PSCRATCH3);
		while (operationaldata != SS_MU_OPERATIONAL) {
			if (time_after(jiffies, before + MU_MAX_DELAY * HZ)) {
				printk(KERN_ERR DRV_NAME
					"(%s): firmware not operational\n",
					pci_name(hba->pdev));
				return -1;
			}
			msleep(1);
			operationaldata = readl(base + PSCRATCH3);
		}
	}

	msg_h = (struct st_msg_header *)hba->dma_mem;
	msg_h->handle = cpu_to_le64(hba->dma_handle);
	msg_h->flag = SS_HEAD_HANDSHAKE;

	h = (struct handshake_frame *)(msg_h + 1);
	h->rb_phy = cpu_to_le64(hba->dma_handle);
	h->req_sz = cpu_to_le16(hba->rq_size);
	h->req_cnt = cpu_to_le16(hba->rq_count+1);
	h->status_sz = cpu_to_le16(sizeof(struct status_msg));
	h->status_cnt = cpu_to_le16(hba->sts_count+1);
	h->hosttime = cpu_to_le64(ktime_get_real_seconds());
	h->partner_type = HMU_PARTNER_TYPE;
	h->extra_offset = h->extra_size = 0;
	scratch_size = (hba->sts_count+1)*sizeof(u32);
	h->scratch_size = cpu_to_le32(scratch_size);

	if (hba->cardtype == st_yel) {
		data = readl(base + YINT_EN);
		data &= ~4;
		writel(data, base + YINT_EN);
		writel((hba->dma_handle >> 16) >> 16, base + YH2I_REQ_HI);
		readl(base + YH2I_REQ_HI);
		writel(hba->dma_handle, base + YH2I_REQ);
		readl(base + YH2I_REQ); /* flush */
	} else {
		data = readl(base + YINT_EN);
		data &= ~(1 << 0);
		data &= ~(1 << 2);
		writel(data, base + YINT_EN);
		if (hba->msi_lock == 0) {
			/* P3 MSI Register cannot access twice */
			writel((1 << 6), base + YH2I_INT);
			hba->msi_lock  = 1;
		}
		writel((hba->dma_handle >> 16) >> 16, base + YH2I_REQ_HI);
		writel(hba->dma_handle, base + YH2I_REQ);
	}

	before = jiffies;
	scratch = hba->scratch;
	if (hba->cardtype == st_yel) {
		while (!(le32_to_cpu(*scratch) & SS_STS_HANDSHAKE)) {
			if (time_after(jiffies, before + MU_MAX_DELAY * HZ)) {
				printk(KERN_ERR DRV_NAME
					"(%s): no signature after handshake frame\n",
					pci_name(hba->pdev));
				ret = -1;
				break;
			}
			rmb();
			msleep(1);
		}
	} else {
		mailboxdata = readl(base + MAILBOX_BASE + MAILBOX_HNDSHK_STS);
		while (mailboxdata != SS_STS_HANDSHAKE) {
			if (time_after(jiffies, before + MU_MAX_DELAY * HZ)) {
				printk(KERN_ERR DRV_NAME
					"(%s): no signature after handshake frame\n",
					pci_name(hba->pdev));
				ret = -1;
				break;
			}
			rmb();
			msleep(1);
			mailboxdata = readl(base + MAILBOX_BASE + MAILBOX_HNDSHK_STS);
		}
	}
	memset(scratch, 0, scratch_size);
	msg_h->flag = 0;

	return ret;
}

static int stex_handshake(struct st_hba *hba)
{
	int err;
	unsigned long flags;
	unsigned int mu_status;

	if (hba->cardtype == st_yel || hba->cardtype == st_P3)
		err = stex_ss_handshake(hba);
	else
		err = stex_common_handshake(hba);
	spin_lock_irqsave(hba->host->host_lock, flags);
	mu_status = hba->mu_status;
	if (err == 0) {
		hba->req_head = 0;
		hba->req_tail = 0;
		hba->status_head = 0;
		hba->status_tail = 0;
		hba->out_req_cnt = 0;
		hba->mu_status = MU_STATE_STARTED;
	} else
		hba->mu_status = MU_STATE_FAILED;
	if (mu_status == MU_STATE_RESETTING)
		wake_up_all(&hba->reset_waitq);
	spin_unlock_irqrestore(hba->host->host_lock, flags);
	return err;
}

static int stex_abort(struct scsi_cmnd *cmd)
{
	struct Scsi_Host *host = cmd->device->host;
	struct st_hba *hba = (struct st_hba *)host->hostdata;
	u16 tag = scsi_cmd_to_rq(cmd)->tag;
	void __iomem *base;
	u32 data;
	int result = SUCCESS;
	unsigned long flags;

	scmd_printk(KERN_INFO, cmd, "aborting command\n");

	base = hba->mmio_base;
	spin_lock_irqsave(host->host_lock, flags);
	if (tag < host->can_queue &&
		hba->ccb[tag].req && hba->ccb[tag].cmd == cmd)
		hba->wait_ccb = &hba->ccb[tag];
	else
		goto out;

	if (hba->cardtype == st_yel) {
		data = readl(base + YI2H_INT);
		if (data == 0 || data == 0xffffffff)
			goto fail_out;

		writel(data, base + YI2H_INT_C);
		stex_ss_mu_intr(hba);
	} else if (hba->cardtype == st_P3) {
		data = readl(base + PSCRATCH4);
		if (data == 0xffffffff)
			goto fail_out;
		if (data != 0) {
			writel(data, base + PSCRATCH1);
			writel((1 << 22), base + YH2I_INT);
		}
		stex_ss_mu_intr(hba);
	} else {
		data = readl(base + ODBL);
		if (data == 0 || data == 0xffffffff)
			goto fail_out;

		writel(data, base + ODBL);
		readl(base + ODBL); /* flush */
		stex_mu_intr(hba, data);
	}
	if (hba->wait_ccb == NULL) {
		printk(KERN_WARNING DRV_NAME
			"(%s): lost interrupt\n", pci_name(hba->pdev));
		goto out;
	}

fail_out:
	scsi_dma_unmap(cmd);
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

	/*
	 * 1 ms may be enough for 8-port controllers. But 16-port controllers
	 * require more time to finish bus reset. Use 100 ms here for safety
	 */
	msleep(100);
	pci_bctl &= ~PCI_BRIDGE_CTL_BUS_RESET;
	pci_write_config_byte(bus->self, PCI_BRIDGE_CONTROL, pci_bctl);

	for (i = 0; i < MU_HARD_RESET_WAIT; i++) {
		pci_read_config_word(hba->pdev, PCI_COMMAND, &pci_cmd);
		if (pci_cmd != 0xffff && (pci_cmd & PCI_COMMAND_MASTER))
			break;
		msleep(1);
	}

	ssleep(5);
	for (i = 0; i < 16; i++)
		pci_write_config_dword(hba->pdev, i * 4,
			hba->pdev->saved_config_space[i]);
}

static int stex_yos_reset(struct st_hba *hba)
{
	void __iomem *base;
	unsigned long flags, before;
	int ret = 0;

	base = hba->mmio_base;
	writel(MU_INBOUND_DOORBELL_RESET, base + IDBL);
	readl(base + IDBL); /* flush */
	before = jiffies;
	while (hba->out_req_cnt > 0) {
		if (time_after(jiffies, before + ST_INTERNAL_TIMEOUT * HZ)) {
			printk(KERN_WARNING DRV_NAME
				"(%s): reset timeout\n", pci_name(hba->pdev));
			ret = -1;
			break;
		}
		msleep(1);
	}

	spin_lock_irqsave(hba->host->host_lock, flags);
	if (ret == -1)
		hba->mu_status = MU_STATE_FAILED;
	else
		hba->mu_status = MU_STATE_STARTED;
	wake_up_all(&hba->reset_waitq);
	spin_unlock_irqrestore(hba->host->host_lock, flags);

	return ret;
}

static void stex_ss_reset(struct st_hba *hba)
{
	writel(SS_H2I_INT_RESET, hba->mmio_base + YH2I_INT);
	readl(hba->mmio_base + YH2I_INT);
	ssleep(5);
}

static void stex_p3_reset(struct st_hba *hba)
{
	writel(SS_H2I_INT_RESET, hba->mmio_base + YH2I_INT);
	ssleep(5);
}

static int stex_do_reset(struct st_hba *hba)
{
	unsigned long flags;
	unsigned int mu_status = MU_STATE_RESETTING;

	spin_lock_irqsave(hba->host->host_lock, flags);
	if (hba->mu_status == MU_STATE_STARTING) {
		spin_unlock_irqrestore(hba->host->host_lock, flags);
		printk(KERN_INFO DRV_NAME "(%s): request reset during init\n",
			pci_name(hba->pdev));
		return 0;
	}
	while (hba->mu_status == MU_STATE_RESETTING) {
		spin_unlock_irqrestore(hba->host->host_lock, flags);
		wait_event_timeout(hba->reset_waitq,
				   hba->mu_status != MU_STATE_RESETTING,
				   MU_MAX_DELAY * HZ);
		spin_lock_irqsave(hba->host->host_lock, flags);
		mu_status = hba->mu_status;
	}

	if (mu_status != MU_STATE_RESETTING) {
		spin_unlock_irqrestore(hba->host->host_lock, flags);
		return (mu_status == MU_STATE_STARTED) ? 0 : -1;
	}

	hba->mu_status = MU_STATE_RESETTING;
	spin_unlock_irqrestore(hba->host->host_lock, flags);

	if (hba->cardtype == st_yosemite)
		return stex_yos_reset(hba);

	if (hba->cardtype == st_shasta)
		stex_hard_reset(hba);
	else if (hba->cardtype == st_yel)
		stex_ss_reset(hba);
	else if (hba->cardtype == st_P3)
		stex_p3_reset(hba);

	return_abnormal_state(hba, DID_RESET);

	if (stex_handshake(hba) == 0)
		return 0;

	printk(KERN_WARNING DRV_NAME "(%s): resetting: handshake failed\n",
		pci_name(hba->pdev));
	return -1;
}

static int stex_reset(struct scsi_cmnd *cmd)
{
	struct st_hba *hba;

	hba = (struct st_hba *) &cmd->device->host->hostdata[0];

	shost_printk(KERN_INFO, cmd->device->host,
		     "resetting host\n");

	return stex_do_reset(hba) ? FAILED : SUCCESS;
}

static void stex_reset_work(struct work_struct *work)
{
	struct st_hba *hba = container_of(work, struct st_hba, reset_work);

	stex_do_reset(hba);
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
	.slave_configure		= stex_slave_config,
	.eh_abort_handler		= stex_abort,
	.eh_host_reset_handler		= stex_reset,
	.this_id			= -1,
	.dma_boundary			= PAGE_SIZE - 1,
};

static struct pci_device_id stex_pci_tbl[] = {
	/* st_shasta */
	{ 0x105a, 0x8350, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		st_shasta }, /* SuperTrak EX8350/8300/16350/16300 */
	{ 0x105a, 0xc350, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		st_shasta }, /* SuperTrak EX12350 */
	{ 0x105a, 0x4302, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		st_shasta }, /* SuperTrak EX4350 */
	{ 0x105a, 0xe350, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		st_shasta }, /* SuperTrak EX24350 */

	/* st_vsc */
	{ 0x105a, 0x7250, PCI_ANY_ID, PCI_ANY_ID, 0, 0, st_vsc },

	/* st_yosemite */
	{ 0x105a, 0x8650, 0x105a, PCI_ANY_ID, 0, 0, st_yosemite },

	/* st_seq */
	{ 0x105a, 0x3360, PCI_ANY_ID, PCI_ANY_ID, 0, 0, st_seq },

	/* st_yel */
	{ 0x105a, 0x8650, 0x1033, PCI_ANY_ID, 0, 0, st_yel },
	{ 0x105a, 0x8760, PCI_ANY_ID, PCI_ANY_ID, 0, 0, st_yel },

	/* st_P3, pluto */
	{ PCI_VENDOR_ID_PROMISE, 0x8870, PCI_VENDOR_ID_PROMISE,
		0x8870, 0, 0, st_P3 },
	/* st_P3, p3 */
	{ PCI_VENDOR_ID_PROMISE, 0x8870, PCI_VENDOR_ID_PROMISE,
		0x4300, 0, 0, st_P3 },

	/* st_P3, SymplyStor4E */
	{ PCI_VENDOR_ID_PROMISE, 0x8871, PCI_VENDOR_ID_PROMISE,
		0x4311, 0, 0, st_P3 },
	/* st_P3, SymplyStor8E */
	{ PCI_VENDOR_ID_PROMISE, 0x8871, PCI_VENDOR_ID_PROMISE,
		0x4312, 0, 0, st_P3 },
	/* st_P3, SymplyStor4 */
	{ PCI_VENDOR_ID_PROMISE, 0x8871, PCI_VENDOR_ID_PROMISE,
		0x4321, 0, 0, st_P3 },
	/* st_P3, SymplyStor8 */
	{ PCI_VENDOR_ID_PROMISE, 0x8871, PCI_VENDOR_ID_PROMISE,
		0x4322, 0, 0, st_P3 },
	{ }	/* terminate list */
};

static struct st_card_info stex_card_info[] = {
	/* st_shasta */
	{
		.max_id		= 17,
		.max_lun	= 8,
		.max_channel	= 0,
		.rq_count	= 32,
		.rq_size	= 1048,
		.sts_count	= 32,
		.alloc_rq	= stex_alloc_req,
		.map_sg		= stex_map_sg,
		.send		= stex_send_cmd,
	},

	/* st_vsc */
	{
		.max_id		= 129,
		.max_lun	= 1,
		.max_channel	= 0,
		.rq_count	= 32,
		.rq_size	= 1048,
		.sts_count	= 32,
		.alloc_rq	= stex_alloc_req,
		.map_sg		= stex_map_sg,
		.send		= stex_send_cmd,
	},

	/* st_yosemite */
	{
		.max_id		= 2,
		.max_lun	= 256,
		.max_channel	= 0,
		.rq_count	= 256,
		.rq_size	= 1048,
		.sts_count	= 256,
		.alloc_rq	= stex_alloc_req,
		.map_sg		= stex_map_sg,
		.send		= stex_send_cmd,
	},

	/* st_seq */
	{
		.max_id		= 129,
		.max_lun	= 1,
		.max_channel	= 0,
		.rq_count	= 32,
		.rq_size	= 1048,
		.sts_count	= 32,
		.alloc_rq	= stex_alloc_req,
		.map_sg		= stex_map_sg,
		.send		= stex_send_cmd,
	},

	/* st_yel */
	{
		.max_id		= 129,
		.max_lun	= 256,
		.max_channel	= 3,
		.rq_count	= 801,
		.rq_size	= 512,
		.sts_count	= 801,
		.alloc_rq	= stex_ss_alloc_req,
		.map_sg		= stex_ss_map_sg,
		.send		= stex_ss_send_cmd,
	},

	/* st_P3 */
	{
		.max_id		= 129,
		.max_lun	= 256,
		.max_channel	= 0,
		.rq_count	= 801,
		.rq_size	= 512,
		.sts_count	= 801,
		.alloc_rq	= stex_ss_alloc_req,
		.map_sg		= stex_ss_map_sg,
		.send		= stex_ss_send_cmd,
	},
};

static int stex_request_irq(struct st_hba *hba)
{
	struct pci_dev *pdev = hba->pdev;
	int status;

	if (msi || hba->cardtype == st_P3) {
		status = pci_enable_msi(pdev);
		if (status != 0)
			printk(KERN_ERR DRV_NAME
				"(%s): error %d setting up MSI\n",
				pci_name(pdev), status);
		else
			hba->msi_enabled = 1;
	} else
		hba->msi_enabled = 0;

	status = request_irq(pdev->irq,
		(hba->cardtype == st_yel || hba->cardtype == st_P3) ?
		stex_ss_intr : stex_intr, IRQF_SHARED, DRV_NAME, hba);

	if (status != 0) {
		if (hba->msi_enabled)
			pci_disable_msi(pdev);
	}
	return status;
}

static void stex_free_irq(struct st_hba *hba)
{
	struct pci_dev *pdev = hba->pdev;

	free_irq(pdev->irq, hba);
	if (hba->msi_enabled)
		pci_disable_msi(pdev);
}

static int stex_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct st_hba *hba;
	struct Scsi_Host *host;
	const struct st_card_info *ci = NULL;
	u32 sts_offset, cp_offset, scratch_offset;
	int err;

	err = pci_enable_device(pdev);
	if (err)
		return err;

	pci_set_master(pdev);

	S6flag = 0;
	register_reboot_notifier(&stex_notifier);

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

	hba->mmio_base = pci_ioremap_bar(pdev, 0);
	if ( !hba->mmio_base) {
		printk(KERN_ERR DRV_NAME "(%s): memory map failed\n",
			pci_name(pdev));
		err = -ENOMEM;
		goto out_release_regions;
	}

	err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (err)
		err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (err) {
		printk(KERN_ERR DRV_NAME "(%s): set dma mask failed\n",
			pci_name(pdev));
		goto out_iounmap;
	}

	hba->cardtype = (unsigned int) id->driver_data;
	ci = &stex_card_info[hba->cardtype];
	switch (id->subdevice) {
	case 0x4221:
	case 0x4222:
	case 0x4223:
	case 0x4224:
	case 0x4225:
	case 0x4226:
	case 0x4227:
	case 0x4261:
	case 0x4262:
	case 0x4263:
	case 0x4264:
	case 0x4265:
		break;
	default:
		if (hba->cardtype == st_yel || hba->cardtype == st_P3)
			hba->supports_pm = 1;
	}

	sts_offset = scratch_offset = (ci->rq_count+1) * ci->rq_size;
	if (hba->cardtype == st_yel || hba->cardtype == st_P3)
		sts_offset += (ci->sts_count+1) * sizeof(u32);
	cp_offset = sts_offset + (ci->sts_count+1) * sizeof(struct status_msg);
	hba->dma_size = cp_offset + sizeof(struct st_frame);
	if (hba->cardtype == st_seq ||
		(hba->cardtype == st_vsc && (pdev->subsystem_device & 1))) {
		hba->extra_offset = hba->dma_size;
		hba->dma_size += ST_ADDITIONAL_MEM;
	}
	hba->dma_mem = dma_alloc_coherent(&pdev->dev,
		hba->dma_size, &hba->dma_handle, GFP_KERNEL);
	if (!hba->dma_mem) {
		/* Retry minimum coherent mapping for st_seq and st_vsc */
		if (hba->cardtype == st_seq ||
		    (hba->cardtype == st_vsc && (pdev->subsystem_device & 1))) {
			printk(KERN_WARNING DRV_NAME
				"(%s): allocating min buffer for controller\n",
				pci_name(pdev));
			hba->dma_size = hba->extra_offset
				+ ST_ADDITIONAL_MEM_MIN;
			hba->dma_mem = dma_alloc_coherent(&pdev->dev,
				hba->dma_size, &hba->dma_handle, GFP_KERNEL);
		}

		if (!hba->dma_mem) {
			err = -ENOMEM;
			printk(KERN_ERR DRV_NAME "(%s): dma mem alloc failed\n",
				pci_name(pdev));
			goto out_iounmap;
		}
	}

	hba->ccb = kcalloc(ci->rq_count, sizeof(struct st_ccb), GFP_KERNEL);
	if (!hba->ccb) {
		err = -ENOMEM;
		printk(KERN_ERR DRV_NAME "(%s): ccb alloc failed\n",
			pci_name(pdev));
		goto out_pci_free;
	}

	if (hba->cardtype == st_yel || hba->cardtype == st_P3)
		hba->scratch = (__le32 *)(hba->dma_mem + scratch_offset);
	hba->status_buffer = (struct status_msg *)(hba->dma_mem + sts_offset);
	hba->copy_buffer = hba->dma_mem + cp_offset;
	hba->rq_count = ci->rq_count;
	hba->rq_size = ci->rq_size;
	hba->sts_count = ci->sts_count;
	hba->alloc_rq = ci->alloc_rq;
	hba->map_sg = ci->map_sg;
	hba->send = ci->send;
	hba->mu_status = MU_STATE_STARTING;
	hba->msi_lock = 0;

	if (hba->cardtype == st_yel || hba->cardtype == st_P3)
		host->sg_tablesize = 38;
	else
		host->sg_tablesize = 32;
	host->can_queue = ci->rq_count;
	host->cmd_per_lun = ci->rq_count;
	host->max_id = ci->max_id;
	host->max_lun = ci->max_lun;
	host->max_channel = ci->max_channel;
	host->unique_id = host->host_no;
	host->max_cmd_len = STEX_CDB_LENGTH;

	hba->host = host;
	hba->pdev = pdev;
	init_waitqueue_head(&hba->reset_waitq);

	snprintf(hba->work_q_name, sizeof(hba->work_q_name),
		 "stex_wq_%d", host->host_no);
	hba->work_q = create_singlethread_workqueue(hba->work_q_name);
	if (!hba->work_q) {
		printk(KERN_ERR DRV_NAME "(%s): create workqueue failed\n",
			pci_name(pdev));
		err = -ENOMEM;
		goto out_ccb_free;
	}
	INIT_WORK(&hba->reset_work, stex_reset_work);

	err = stex_request_irq(hba);
	if (err) {
		printk(KERN_ERR DRV_NAME "(%s): request irq failed\n",
			pci_name(pdev));
		goto out_free_wq;
	}

	err = stex_handshake(hba);
	if (err)
		goto out_free_irq;

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
	stex_free_irq(hba);
out_free_wq:
	destroy_workqueue(hba->work_q);
out_ccb_free:
	kfree(hba->ccb);
out_pci_free:
	dma_free_coherent(&pdev->dev, hba->dma_size,
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

static void stex_hba_stop(struct st_hba *hba, int st_sleep_mic)
{
	struct req_msg *req;
	struct st_msg_header *msg_h;
	unsigned long flags;
	unsigned long before;
	u16 tag = 0;

	spin_lock_irqsave(hba->host->host_lock, flags);

	if ((hba->cardtype == st_yel || hba->cardtype == st_P3) &&
		hba->supports_pm == 1) {
		if (st_sleep_mic == ST_NOTHANDLED) {
			spin_unlock_irqrestore(hba->host->host_lock, flags);
			return;
		}
	}
	req = hba->alloc_rq(hba);
	if (hba->cardtype == st_yel || hba->cardtype == st_P3) {
		msg_h = (struct st_msg_header *)req - 1;
		memset(msg_h, 0, hba->rq_size);
	} else
		memset(req, 0, hba->rq_size);

	if ((hba->cardtype == st_yosemite || hba->cardtype == st_yel
		|| hba->cardtype == st_P3)
		&& st_sleep_mic == ST_IGNORED) {
		req->cdb[0] = MGT_CMD;
		req->cdb[1] = MGT_CMD_SIGNATURE;
		req->cdb[2] = CTLR_CONFIG_CMD;
		req->cdb[3] = CTLR_SHUTDOWN;
	} else if ((hba->cardtype == st_yel || hba->cardtype == st_P3)
		&& st_sleep_mic != ST_IGNORED) {
		req->cdb[0] = MGT_CMD;
		req->cdb[1] = MGT_CMD_SIGNATURE;
		req->cdb[2] = CTLR_CONFIG_CMD;
		req->cdb[3] = PMIC_SHUTDOWN;
		req->cdb[4] = st_sleep_mic;
	} else {
		req->cdb[0] = CONTROLLER_CMD;
		req->cdb[1] = CTLR_POWER_STATE_CHANGE;
		req->cdb[2] = CTLR_POWER_SAVING;
	}
	hba->ccb[tag].cmd = NULL;
	hba->ccb[tag].sg_count = 0;
	hba->ccb[tag].sense_bufflen = 0;
	hba->ccb[tag].sense_buffer = NULL;
	hba->ccb[tag].req_type = PASSTHRU_REQ_TYPE;
	hba->send(hba, req, tag);
	spin_unlock_irqrestore(hba->host->host_lock, flags);
	before = jiffies;
	while (hba->ccb[tag].req_type & PASSTHRU_REQ_TYPE) {
		if (time_after(jiffies, before + ST_INTERNAL_TIMEOUT * HZ)) {
			hba->ccb[tag].req_type = 0;
			hba->mu_status = MU_STATE_STOP;
			return;
		}
		msleep(1);
	}
	hba->mu_status = MU_STATE_STOP;
}

static void stex_hba_free(struct st_hba *hba)
{
	stex_free_irq(hba);

	destroy_workqueue(hba->work_q);

	iounmap(hba->mmio_base);

	pci_release_regions(hba->pdev);

	kfree(hba->ccb);

	dma_free_coherent(&hba->pdev->dev, hba->dma_size,
			  hba->dma_mem, hba->dma_handle);
}

static void stex_remove(struct pci_dev *pdev)
{
	struct st_hba *hba = pci_get_drvdata(pdev);

	hba->mu_status = MU_STATE_NOCONNECT;
	return_abnormal_state(hba, DID_NO_CONNECT);
	scsi_remove_host(hba->host);

	scsi_block_requests(hba->host);

	stex_hba_free(hba);

	scsi_host_put(hba->host);

	pci_disable_device(pdev);

	unregister_reboot_notifier(&stex_notifier);
}

static void stex_shutdown(struct pci_dev *pdev)
{
	struct st_hba *hba = pci_get_drvdata(pdev);

	if (hba->supports_pm == 0) {
		stex_hba_stop(hba, ST_IGNORED);
	} else if (hba->supports_pm == 1 && S6flag) {
		unregister_reboot_notifier(&stex_notifier);
		stex_hba_stop(hba, ST_S6);
	} else
		stex_hba_stop(hba, ST_S5);
}

static int stex_choice_sleep_mic(struct st_hba *hba, pm_message_t state)
{
	switch (state.event) {
	case PM_EVENT_SUSPEND:
		return ST_S3;
	case PM_EVENT_HIBERNATE:
		hba->msi_lock = 0;
		return ST_S4;
	default:
		return ST_NOTHANDLED;
	}
}

static int stex_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct st_hba *hba = pci_get_drvdata(pdev);

	if ((hba->cardtype == st_yel || hba->cardtype == st_P3)
		&& hba->supports_pm == 1)
		stex_hba_stop(hba, stex_choice_sleep_mic(hba, state));
	else
		stex_hba_stop(hba, ST_IGNORED);
	return 0;
}

static int stex_resume(struct pci_dev *pdev)
{
	struct st_hba *hba = pci_get_drvdata(pdev);

	hba->mu_status = MU_STATE_STARTING;
	stex_handshake(hba);
	return 0;
}

static int stex_halt(struct notifier_block *nb, unsigned long event, void *buf)
{
	S6flag = 1;
	return NOTIFY_OK;
}
MODULE_DEVICE_TABLE(pci, stex_pci_tbl);

static struct pci_driver stex_pci_driver = {
	.name		= DRV_NAME,
	.id_table	= stex_pci_tbl,
	.probe		= stex_probe,
	.remove		= stex_remove,
	.shutdown	= stex_shutdown,
	.suspend	= stex_suspend,
	.resume		= stex_resume,
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
