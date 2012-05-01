/*
 *    Disk Array driver for HP Smart Array SAS controllers
 *    Copyright 2000, 2009 Hewlett-Packard Development Company, L.P.
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; version 2 of the License.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *    NON INFRINGEMENT.  See the GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *    Questions/Comments/Bugfixes to iss_storagedev@hp.com
 *
 */
#ifndef HPSA_H
#define HPSA_H

#include <scsi/scsicam.h>

#define IO_OK		0
#define IO_ERROR	1

struct ctlr_info;

struct access_method {
	void (*submit_command)(struct ctlr_info *h,
		struct CommandList *c);
	void (*set_intr_mask)(struct ctlr_info *h, unsigned long val);
	unsigned long (*fifo_full)(struct ctlr_info *h);
	bool (*intr_pending)(struct ctlr_info *h);
	unsigned long (*command_completed)(struct ctlr_info *h, u8 q);
};

struct hpsa_scsi_dev_t {
	int devtype;
	int bus, target, lun;		/* as presented to the OS */
	unsigned char scsi3addr[8];	/* as presented to the HW */
#define RAID_CTLR_LUNID "\0\0\0\0\0\0\0\0"
	unsigned char device_id[16];    /* from inquiry pg. 0x83 */
	unsigned char vendor[8];        /* bytes 8-15 of inquiry data */
	unsigned char model[16];        /* bytes 16-31 of inquiry data */
	unsigned char raid_level;	/* from inquiry page 0xC1 */
};

struct reply_pool {
	u64 *head;
	size_t size;
	u8 wraparound;
	u32 current_entry;
};

struct ctlr_info {
	int	ctlr;
	char	devname[8];
	char    *product_name;
	struct pci_dev *pdev;
	u32	board_id;
	void __iomem *vaddr;
	unsigned long paddr;
	int 	nr_cmds; /* Number of commands allowed on this controller */
	struct CfgTable __iomem *cfgtable;
	int	interrupts_enabled;
	int	major;
	int 	max_commands;
	int	commands_outstanding;
	int 	max_outstanding; /* Debug */
	int	usage_count;  /* number of opens all all minor devices */
#	define PERF_MODE_INT	0
#	define DOORBELL_INT	1
#	define SIMPLE_MODE_INT	2
#	define MEMQ_MODE_INT	3
	unsigned int intr[MAX_REPLY_QUEUES];
	unsigned int msix_vector;
	unsigned int msi_vector;
	int intr_mode; /* either PERF_MODE_INT or SIMPLE_MODE_INT */
	struct access_method access;

	/* queue and queue Info */
	struct list_head reqQ;
	struct list_head cmpQ;
	unsigned int Qdepth;
	unsigned int maxSG;
	spinlock_t lock;
	int maxsgentries;
	u8 max_cmd_sg_entries;
	int chainsize;
	struct SGDescriptor **cmd_sg_list;

	/* pointers to command and error info pool */
	struct CommandList 	*cmd_pool;
	dma_addr_t		cmd_pool_dhandle;
	struct ErrorInfo 	*errinfo_pool;
	dma_addr_t		errinfo_pool_dhandle;
	unsigned long  		*cmd_pool_bits;
	int			nr_allocs;
	int			nr_frees;
	int			scan_finished;
	spinlock_t		scan_lock;
	wait_queue_head_t	scan_wait_queue;

	struct Scsi_Host *scsi_host;
	spinlock_t devlock; /* to protect hba[ctlr]->dev[];  */
	int ndevices; /* number of used elements in .dev[] array. */
	struct hpsa_scsi_dev_t *dev[HPSA_MAX_DEVICES];
	/*
	 * Performant mode tables.
	 */
	u32 trans_support;
	u32 trans_offset;
	struct TransTable_struct *transtable;
	unsigned long transMethod;

	/*
	 * Performant mode completion buffers
	 */
	u64 *reply_pool;
	size_t reply_pool_size;
	struct reply_pool reply_queue[MAX_REPLY_QUEUES];
	u8 nreply_queues;
	dma_addr_t reply_pool_dhandle;
	u32 *blockFetchTable;
	unsigned char *hba_inquiry_data;
	u64 last_intr_timestamp;
	u32 last_heartbeat;
	u64 last_heartbeat_timestamp;
	u32 lockup_detected;
	struct list_head lockup_list;
	/* Address of h->q[x] is passed to intr handler to know which queue */
	u8 q[MAX_REPLY_QUEUES];
	u32 TMFSupportFlags; /* cache what task mgmt funcs are supported. */
#define HPSATMF_BITS_SUPPORTED  (1 << 0)
#define HPSATMF_PHYS_LUN_RESET  (1 << 1)
#define HPSATMF_PHYS_NEX_RESET  (1 << 2)
#define HPSATMF_PHYS_TASK_ABORT (1 << 3)
#define HPSATMF_PHYS_TSET_ABORT (1 << 4)
#define HPSATMF_PHYS_CLEAR_ACA  (1 << 5)
#define HPSATMF_PHYS_CLEAR_TSET (1 << 6)
#define HPSATMF_PHYS_QRY_TASK   (1 << 7)
#define HPSATMF_PHYS_QRY_TSET   (1 << 8)
#define HPSATMF_PHYS_QRY_ASYNC  (1 << 9)
#define HPSATMF_MASK_SUPPORTED  (1 << 16)
#define HPSATMF_LOG_LUN_RESET   (1 << 17)
#define HPSATMF_LOG_NEX_RESET   (1 << 18)
#define HPSATMF_LOG_TASK_ABORT  (1 << 19)
#define HPSATMF_LOG_TSET_ABORT  (1 << 20)
#define HPSATMF_LOG_CLEAR_ACA   (1 << 21)
#define HPSATMF_LOG_CLEAR_TSET  (1 << 22)
#define HPSATMF_LOG_QRY_TASK    (1 << 23)
#define HPSATMF_LOG_QRY_TSET    (1 << 24)
#define HPSATMF_LOG_QRY_ASYNC   (1 << 25)
};
#define HPSA_ABORT_MSG 0
#define HPSA_DEVICE_RESET_MSG 1
#define HPSA_RESET_TYPE_CONTROLLER 0x00
#define HPSA_RESET_TYPE_BUS 0x01
#define HPSA_RESET_TYPE_TARGET 0x03
#define HPSA_RESET_TYPE_LUN 0x04
#define HPSA_MSG_SEND_RETRY_LIMIT 10
#define HPSA_MSG_SEND_RETRY_INTERVAL_MSECS (10000)

/* Maximum time in seconds driver will wait for command completions
 * when polling before giving up.
 */
#define HPSA_MAX_POLL_TIME_SECS (20)

/* During SCSI error recovery, HPSA_TUR_RETRY_LIMIT defines
 * how many times to retry TEST UNIT READY on a device
 * while waiting for it to become ready before giving up.
 * HPSA_MAX_WAIT_INTERVAL_SECS is the max wait interval
 * between sending TURs while waiting for a device
 * to become ready.
 */
#define HPSA_TUR_RETRY_LIMIT (20)
#define HPSA_MAX_WAIT_INTERVAL_SECS (30)

/* HPSA_BOARD_READY_WAIT_SECS is how long to wait for a board
 * to become ready, in seconds, before giving up on it.
 * HPSA_BOARD_READY_POLL_INTERVAL_MSECS * is how long to wait
 * between polling the board to see if it is ready, in
 * milliseconds.  HPSA_BOARD_READY_POLL_INTERVAL and
 * HPSA_BOARD_READY_ITERATIONS are derived from those.
 */
#define HPSA_BOARD_READY_WAIT_SECS (120)
#define HPSA_BOARD_NOT_READY_WAIT_SECS (100)
#define HPSA_BOARD_READY_POLL_INTERVAL_MSECS (100)
#define HPSA_BOARD_READY_POLL_INTERVAL \
	((HPSA_BOARD_READY_POLL_INTERVAL_MSECS * HZ) / 1000)
#define HPSA_BOARD_READY_ITERATIONS \
	((HPSA_BOARD_READY_WAIT_SECS * 1000) / \
		HPSA_BOARD_READY_POLL_INTERVAL_MSECS)
#define HPSA_BOARD_NOT_READY_ITERATIONS \
	((HPSA_BOARD_NOT_READY_WAIT_SECS * 1000) / \
		HPSA_BOARD_READY_POLL_INTERVAL_MSECS)
#define HPSA_POST_RESET_PAUSE_MSECS (3000)
#define HPSA_POST_RESET_NOOP_RETRIES (12)

/*  Defining the diffent access_menthods */
/*
 * Memory mapped FIFO interface (SMART 53xx cards)
 */
#define SA5_DOORBELL	0x20
#define SA5_REQUEST_PORT_OFFSET	0x40
#define SA5_REPLY_INTR_MASK_OFFSET	0x34
#define SA5_REPLY_PORT_OFFSET		0x44
#define SA5_INTR_STATUS		0x30
#define SA5_SCRATCHPAD_OFFSET	0xB0

#define SA5_CTCFG_OFFSET	0xB4
#define SA5_CTMEM_OFFSET	0xB8

#define SA5_INTR_OFF		0x08
#define SA5B_INTR_OFF		0x04
#define SA5_INTR_PENDING	0x08
#define SA5B_INTR_PENDING	0x04
#define FIFO_EMPTY		0xffffffff
#define HPSA_FIRMWARE_READY	0xffff0000 /* value in scratchpad register */

#define HPSA_ERROR_BIT		0x02

/* Performant mode flags */
#define SA5_PERF_INTR_PENDING   0x04
#define SA5_PERF_INTR_OFF       0x05
#define SA5_OUTDB_STATUS_PERF_BIT       0x01
#define SA5_OUTDB_CLEAR_PERF_BIT        0x01
#define SA5_OUTDB_CLEAR         0xA0
#define SA5_OUTDB_CLEAR_PERF_BIT        0x01
#define SA5_OUTDB_STATUS        0x9C


#define HPSA_INTR_ON 	1
#define HPSA_INTR_OFF	0
/*
	Send the command to the hardware
*/
static void SA5_submit_command(struct ctlr_info *h,
	struct CommandList *c)
{
	dev_dbg(&h->pdev->dev, "Sending %x, tag = %x\n", c->busaddr,
		c->Header.Tag.lower);
	writel(c->busaddr, h->vaddr + SA5_REQUEST_PORT_OFFSET);
	(void) readl(h->vaddr + SA5_SCRATCHPAD_OFFSET);
}

/*
 *  This card is the opposite of the other cards.
 *   0 turns interrupts on...
 *   0x08 turns them off...
 */
static void SA5_intr_mask(struct ctlr_info *h, unsigned long val)
{
	if (val) { /* Turn interrupts on */
		h->interrupts_enabled = 1;
		writel(0, h->vaddr + SA5_REPLY_INTR_MASK_OFFSET);
		(void) readl(h->vaddr + SA5_REPLY_INTR_MASK_OFFSET);
	} else { /* Turn them off */
		h->interrupts_enabled = 0;
		writel(SA5_INTR_OFF,
			h->vaddr + SA5_REPLY_INTR_MASK_OFFSET);
		(void) readl(h->vaddr + SA5_REPLY_INTR_MASK_OFFSET);
	}
}

static void SA5_performant_intr_mask(struct ctlr_info *h, unsigned long val)
{
	if (val) { /* turn on interrupts */
		h->interrupts_enabled = 1;
		writel(0, h->vaddr + SA5_REPLY_INTR_MASK_OFFSET);
		(void) readl(h->vaddr + SA5_REPLY_INTR_MASK_OFFSET);
	} else {
		h->interrupts_enabled = 0;
		writel(SA5_PERF_INTR_OFF,
			h->vaddr + SA5_REPLY_INTR_MASK_OFFSET);
		(void) readl(h->vaddr + SA5_REPLY_INTR_MASK_OFFSET);
	}
}

static unsigned long SA5_performant_completed(struct ctlr_info *h, u8 q)
{
	struct reply_pool *rq = &h->reply_queue[q];
	unsigned long flags, register_value = FIFO_EMPTY;

	/* msi auto clears the interrupt pending bit. */
	if (!(h->msi_vector || h->msix_vector)) {
		/* flush the controller write of the reply queue by reading
		 * outbound doorbell status register.
		 */
		register_value = readl(h->vaddr + SA5_OUTDB_STATUS);
		writel(SA5_OUTDB_CLEAR_PERF_BIT, h->vaddr + SA5_OUTDB_CLEAR);
		/* Do a read in order to flush the write to the controller
		 * (as per spec.)
		 */
		register_value = readl(h->vaddr + SA5_OUTDB_STATUS);
	}

	if ((rq->head[rq->current_entry] & 1) == rq->wraparound) {
		register_value = rq->head[rq->current_entry];
		rq->current_entry++;
		spin_lock_irqsave(&h->lock, flags);
		h->commands_outstanding--;
		spin_unlock_irqrestore(&h->lock, flags);
	} else {
		register_value = FIFO_EMPTY;
	}
	/* Check for wraparound */
	if (rq->current_entry == h->max_commands) {
		rq->current_entry = 0;
		rq->wraparound ^= 1;
	}
	return register_value;
}

/*
 *  Returns true if fifo is full.
 *
 */
static unsigned long SA5_fifo_full(struct ctlr_info *h)
{
	if (h->commands_outstanding >= h->max_commands)
		return 1;
	else
		return 0;

}
/*
 *   returns value read from hardware.
 *     returns FIFO_EMPTY if there is nothing to read
 */
static unsigned long SA5_completed(struct ctlr_info *h,
	__attribute__((unused)) u8 q)
{
	unsigned long register_value
		= readl(h->vaddr + SA5_REPLY_PORT_OFFSET);
	unsigned long flags;

	if (register_value != FIFO_EMPTY) {
		spin_lock_irqsave(&h->lock, flags);
		h->commands_outstanding--;
		spin_unlock_irqrestore(&h->lock, flags);
	}

#ifdef HPSA_DEBUG
	if (register_value != FIFO_EMPTY)
		dev_dbg(&h->pdev->dev, "Read %lx back from board\n",
			register_value);
	else
		dev_dbg(&h->pdev->dev, "FIFO Empty read\n");
#endif

	return register_value;
}
/*
 *	Returns true if an interrupt is pending..
 */
static bool SA5_intr_pending(struct ctlr_info *h)
{
	unsigned long register_value  =
		readl(h->vaddr + SA5_INTR_STATUS);
	dev_dbg(&h->pdev->dev, "intr_pending %lx\n", register_value);
	return register_value & SA5_INTR_PENDING;
}

static bool SA5_performant_intr_pending(struct ctlr_info *h)
{
	unsigned long register_value = readl(h->vaddr + SA5_INTR_STATUS);

	if (!register_value)
		return false;

	if (h->msi_vector || h->msix_vector)
		return true;

	/* Read outbound doorbell to flush */
	register_value = readl(h->vaddr + SA5_OUTDB_STATUS);
	return register_value & SA5_OUTDB_STATUS_PERF_BIT;
}

static struct access_method SA5_access = {
	SA5_submit_command,
	SA5_intr_mask,
	SA5_fifo_full,
	SA5_intr_pending,
	SA5_completed,
};

static struct access_method SA5_performant_access = {
	SA5_submit_command,
	SA5_performant_intr_mask,
	SA5_fifo_full,
	SA5_performant_intr_pending,
	SA5_performant_completed,
};

struct board_type {
	u32	board_id;
	char	*product_name;
	struct access_method *access;
};

#endif /* HPSA_H */

