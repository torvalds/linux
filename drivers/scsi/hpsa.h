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
	unsigned long (*intr_pending)(struct ctlr_info *h);
	unsigned long (*command_completed)(struct ctlr_info *h);
};

struct hpsa_scsi_dev_t {
	int devtype;
	int bus, target, lun;		/* as presented to the OS */
	unsigned char scsi3addr[8];	/* as presented to the HW */
#define RAID_CTLR_LUNID "\0\0\0\0\0\0\0\0"
	unsigned char device_id[16];    /* from inquiry pg. 0x83 */
	unsigned char vendor[8];        /* bytes 8-15 of inquiry data */
	unsigned char model[16];        /* bytes 16-31 of inquiry data */
	unsigned char revision[4];      /* bytes 32-35 of inquiry data */
	unsigned char raid_level;	/* from inquiry page 0xC1 */
};

struct ctlr_info {
	int	ctlr;
	char	devname[8];
	char    *product_name;
	char	firm_ver[4]; /* Firmware version */
	struct pci_dev *pdev;
	__u32	board_id;
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
#	define DOORBELL_INT	0
#	define PERF_MODE_INT	1
#	define SIMPLE_MODE_INT	2
#	define MEMQ_MODE_INT	3
	unsigned int intr[4];
	unsigned int msix_vector;
	unsigned int msi_vector;
	struct access_method access;

	/* queue and queue Info */
	struct hlist_head reqQ;
	struct hlist_head cmpQ;
	unsigned int Qdepth;
	unsigned int maxQsinceinit;
	unsigned int maxSG;
	spinlock_t lock;

	/* pointers to command and error info pool */
	struct CommandList 	*cmd_pool;
	dma_addr_t		cmd_pool_dhandle;
	struct ErrorInfo 	*errinfo_pool;
	dma_addr_t		errinfo_pool_dhandle;
	unsigned long  		*cmd_pool_bits;
	int			nr_allocs;
	int			nr_frees;
	int			busy_initializing;
	int			busy_scanning;
	struct mutex		busy_shutting_down;
	struct list_head	scan_list;
	struct completion	scan_wait;

	struct Scsi_Host *scsi_host;
	spinlock_t devlock; /* to protect hba[ctlr]->dev[];  */
	int ndevices; /* number of used elements in .dev[] array. */
#define HPSA_MAX_SCSI_DEVS_PER_HBA 256
	struct hpsa_scsi_dev_t *dev[HPSA_MAX_SCSI_DEVS_PER_HBA];
};
#define HPSA_ABORT_MSG 0
#define HPSA_DEVICE_RESET_MSG 1
#define HPSA_BUS_RESET_MSG 2
#define HPSA_HOST_RESET_MSG 3
#define HPSA_MSG_SEND_RETRY_LIMIT 10
#define HPSA_MSG_SEND_RETRY_INTERVAL_MSECS 1000

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
#define HPSA_BOARD_READY_POLL_INTERVAL_MSECS (100)
#define HPSA_BOARD_READY_POLL_INTERVAL \
	((HPSA_BOARD_READY_POLL_INTERVAL_MSECS * HZ) / 1000)
#define HPSA_BOARD_READY_ITERATIONS \
	((HPSA_BOARD_READY_WAIT_SECS * 1000) / \
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
#define HPSA_TAG_CONTAINS_INDEX(tag) ((tag) & 0x04)
#define HPSA_TAG_TO_INDEX(tag) ((tag) >> 3)
#define HPSA_TAG_DISCARD_ERROR_BITS(tag) ((tag) & ~3)

#define HPSA_INTR_ON 	1
#define HPSA_INTR_OFF	0
/*
	Send the command to the hardware
*/
static void SA5_submit_command(struct ctlr_info *h,
	struct CommandList *c)
{
#ifdef HPSA_DEBUG
	 printk(KERN_WARNING "hpsa: Sending %x - down to controller\n",
		c->busaddr);
#endif /* HPSA_DEBUG */
	writel(c->busaddr, h->vaddr + SA5_REQUEST_PORT_OFFSET);
	h->commands_outstanding++;
	if (h->commands_outstanding > h->max_outstanding)
		h->max_outstanding = h->commands_outstanding;
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
	} else { /* Turn them off */
		h->interrupts_enabled = 0;
		writel(SA5_INTR_OFF,
			h->vaddr + SA5_REPLY_INTR_MASK_OFFSET);
	}
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
static unsigned long SA5_completed(struct ctlr_info *h)
{
	unsigned long register_value
		= readl(h->vaddr + SA5_REPLY_PORT_OFFSET);

	if (register_value != FIFO_EMPTY)
		h->commands_outstanding--;

#ifdef HPSA_DEBUG
	if (register_value != FIFO_EMPTY)
		printk(KERN_INFO "hpsa:  Read %lx back from board\n",
			register_value);
	else
		printk(KERN_INFO "hpsa:  FIFO Empty read\n");
#endif

	return register_value;
}
/*
 *	Returns true if an interrupt is pending..
 */
static unsigned long SA5_intr_pending(struct ctlr_info *h)
{
	unsigned long register_value  =
		readl(h->vaddr + SA5_INTR_STATUS);
#ifdef HPSA_DEBUG
	printk(KERN_INFO "hpsa: intr_pending %lx\n", register_value);
#endif  /* HPSA_DEBUG */
	if (register_value &  SA5_INTR_PENDING)
		return  1;
	return 0 ;
}


static struct access_method SA5_access = {
	SA5_submit_command,
	SA5_intr_mask,
	SA5_fifo_full,
	SA5_intr_pending,
	SA5_completed,
};

struct board_type {
	__u32	board_id;
	char	*product_name;
	struct access_method *access;
};


/* end of old hpsa_scsi.h file */

#endif /* HPSA_H */

