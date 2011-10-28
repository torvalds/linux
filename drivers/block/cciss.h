#ifndef CCISS_H
#define CCISS_H

#include <linux/genhd.h>
#include <linux/mutex.h>

#include "cciss_cmd.h"


#define NWD_SHIFT	4
#define MAX_PART	(1 << NWD_SHIFT)

#define IO_OK		0
#define IO_ERROR	1
#define IO_NEEDS_RETRY  3

#define VENDOR_LEN	8
#define MODEL_LEN	16
#define REV_LEN		4

struct ctlr_info;
typedef struct ctlr_info ctlr_info_t;

struct access_method {
	void (*submit_command)(ctlr_info_t *h, CommandList_struct *c);
	void (*set_intr_mask)(ctlr_info_t *h, unsigned long val);
	unsigned long (*fifo_full)(ctlr_info_t *h);
	bool (*intr_pending)(ctlr_info_t *h);
	unsigned long (*command_completed)(ctlr_info_t *h);
};
typedef struct _drive_info_struct
{
	unsigned char LunID[8];
	int 	usage_count;
	struct request_queue *queue;
	sector_t nr_blocks;
	int	block_size;
	int 	heads;
	int	sectors;
	int 	cylinders;
	int	raid_level; /* set to -1 to indicate that
			     * the drive is not in use/configured
			     */
	int	busy_configuring; /* This is set when a drive is being removed
				   * to prevent it from being opened or it's
				   * queue from being started.
				   */
	struct	device dev;
	__u8 serial_no[16]; /* from inquiry page 0x83,
			     * not necc. null terminated.
			     */
	char vendor[VENDOR_LEN + 1]; /* SCSI vendor string */
	char model[MODEL_LEN + 1];   /* SCSI model string */
	char rev[REV_LEN + 1];       /* SCSI revision string */
	char device_initialized;     /* indicates whether dev is initialized */
} drive_info_struct;

struct ctlr_info
{
	int	ctlr;
	char	devname[8];
	char    *product_name;
	char	firm_ver[4]; /* Firmware version */
	struct pci_dev *pdev;
	__u32	board_id;
	void __iomem *vaddr;
	unsigned long paddr;
	int 	nr_cmds; /* Number of commands allowed on this controller */
	CfgTable_struct __iomem *cfgtable;
	int	interrupts_enabled;
	int	major;
	int 	max_commands;
	int	commands_outstanding;
	int 	max_outstanding; /* Debug */ 
	int	num_luns;
	int 	highest_lun;
	int	usage_count;  /* number of opens all all minor devices */
	/* Need space for temp sg list
	 * number of scatter/gathers supported
	 * number of scatter/gathers in chained block
	 */
	struct	scatterlist **scatter_list;
	int	maxsgentries;
	int	chainsize;
	int	max_cmd_sgentries;
	SGDescriptor_struct **cmd_sg_list;

#	define PERF_MODE_INT	0
#	define DOORBELL_INT	1
#	define SIMPLE_MODE_INT	2
#	define MEMQ_MODE_INT	3
	unsigned int intr[4];
	unsigned int msix_vector;
	unsigned int msi_vector;
	int 	cciss_max_sectors;
	BYTE	cciss_read;
	BYTE	cciss_write;
	BYTE	cciss_read_capacity;

	/* information about each logical volume */
	drive_info_struct *drv[CISS_MAX_LUN];

	struct access_method access;

	/* queue and queue Info */ 
	struct list_head reqQ;
	struct list_head cmpQ;
	unsigned int Qdepth;
	unsigned int maxQsinceinit;
	unsigned int maxSG;
	spinlock_t lock;

	/* pointers to command and error info pool */
	CommandList_struct 	*cmd_pool;
	dma_addr_t		cmd_pool_dhandle; 
	ErrorInfo_struct 	*errinfo_pool;
	dma_addr_t		errinfo_pool_dhandle; 
        unsigned long  		*cmd_pool_bits;
	int			nr_allocs;
	int			nr_frees; 
	int			busy_configuring;
	int			busy_initializing;
	int			busy_scanning;
	struct mutex		busy_shutting_down;

	/* This element holds the zero based queue number of the last
	 * queue to be started.  It is used for fairness.
	*/
	int			next_to_run;

	/* Disk structures we need to pass back */
	struct gendisk   *gendisk[CISS_MAX_LUN];
#ifdef CONFIG_CISS_SCSI_TAPE
	struct cciss_scsi_adapter_data_t *scsi_ctlr;
#endif
	unsigned char alive;
	struct list_head scan_list;
	struct completion scan_wait;
	struct device dev;
	/*
	 * Performant mode tables.
	 */
	u32 trans_support;
	u32 trans_offset;
	struct TransTable_struct *transtable;
	unsigned long transMethod;

	/*
	 * Performant mode completion buffer
	 */
	u64 *reply_pool;
	dma_addr_t reply_pool_dhandle;
	u64 *reply_pool_head;
	size_t reply_pool_size;
	unsigned char reply_pool_wraparound;
	u32 *blockFetchTable;
};

/*  Defining the diffent access_methods
 *
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
#define CCISS_FIRMWARE_READY	0xffff0000 /* value in scratchpad register */
/* Perf. mode flags */
#define SA5_PERF_INTR_PENDING	0x04
#define SA5_PERF_INTR_OFF	0x05
#define SA5_OUTDB_STATUS_PERF_BIT	0x01
#define SA5_OUTDB_CLEAR_PERF_BIT	0x01
#define SA5_OUTDB_CLEAR         0xA0
#define SA5_OUTDB_CLEAR_PERF_BIT        0x01
#define SA5_OUTDB_STATUS        0x9C


#define  CISS_ERROR_BIT		0x02

#define CCISS_INTR_ON 	1 
#define CCISS_INTR_OFF	0


/* CCISS_BOARD_READY_WAIT_SECS is how long to wait for a board
 * to become ready, in seconds, before giving up on it.
 * CCISS_BOARD_READY_POLL_INTERVAL_MSECS * is how long to wait
 * between polling the board to see if it is ready, in
 * milliseconds.  CCISS_BOARD_READY_ITERATIONS is derived
 * the above.
 */
#define CCISS_BOARD_READY_WAIT_SECS (120)
#define CCISS_BOARD_NOT_READY_WAIT_SECS (100)
#define CCISS_BOARD_READY_POLL_INTERVAL_MSECS (100)
#define CCISS_BOARD_READY_ITERATIONS \
	((CCISS_BOARD_READY_WAIT_SECS * 1000) / \
		CCISS_BOARD_READY_POLL_INTERVAL_MSECS)
#define CCISS_BOARD_NOT_READY_ITERATIONS \
	((CCISS_BOARD_NOT_READY_WAIT_SECS * 1000) / \
		CCISS_BOARD_READY_POLL_INTERVAL_MSECS)
#define CCISS_POST_RESET_PAUSE_MSECS (3000)
#define CCISS_POST_RESET_NOOP_INTERVAL_MSECS (4000)
#define CCISS_POST_RESET_NOOP_RETRIES (12)
#define CCISS_POST_RESET_NOOP_TIMEOUT_MSECS (10000)

/* 
	Send the command to the hardware 
*/
static void SA5_submit_command( ctlr_info_t *h, CommandList_struct *c) 
{
#ifdef CCISS_DEBUG
	printk(KERN_WARNING "cciss%d: Sending %08x - down to controller\n",
			h->ctlr, c->busaddr);
#endif /* CCISS_DEBUG */
         writel(c->busaddr, h->vaddr + SA5_REQUEST_PORT_OFFSET);
	readl(h->vaddr + SA5_SCRATCHPAD_OFFSET);
	 h->commands_outstanding++;
	 if ( h->commands_outstanding > h->max_outstanding)
		h->max_outstanding = h->commands_outstanding;
}

/*  
 *  This card is the opposite of the other cards.  
 *   0 turns interrupts on... 
 *   0x08 turns them off... 
 */
static void SA5_intr_mask(ctlr_info_t *h, unsigned long val)
{
	if (val) 
	{ /* Turn interrupts on */
		h->interrupts_enabled = 1;
		writel(0, h->vaddr + SA5_REPLY_INTR_MASK_OFFSET);
		(void) readl(h->vaddr + SA5_REPLY_INTR_MASK_OFFSET);
	} else /* Turn them off */
	{
		h->interrupts_enabled = 0;
        	writel( SA5_INTR_OFF, 
			h->vaddr + SA5_REPLY_INTR_MASK_OFFSET);
		(void) readl(h->vaddr + SA5_REPLY_INTR_MASK_OFFSET);
	}
}
/*
 *  This card is the opposite of the other cards.
 *   0 turns interrupts on...
 *   0x04 turns them off...
 */
static void SA5B_intr_mask(ctlr_info_t *h, unsigned long val)
{
        if (val)
        { /* Turn interrupts on */
		h->interrupts_enabled = 1;
                writel(0, h->vaddr + SA5_REPLY_INTR_MASK_OFFSET);
		(void) readl(h->vaddr + SA5_REPLY_INTR_MASK_OFFSET);
        } else /* Turn them off */
        {
		h->interrupts_enabled = 0;
                writel( SA5B_INTR_OFF,
                        h->vaddr + SA5_REPLY_INTR_MASK_OFFSET);
		(void) readl(h->vaddr + SA5_REPLY_INTR_MASK_OFFSET);
        }
}

/* Performant mode intr_mask */
static void SA5_performant_intr_mask(ctlr_info_t *h, unsigned long val)
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

/*
 *  Returns true if fifo is full.  
 * 
 */ 
static unsigned long SA5_fifo_full(ctlr_info_t *h)
{
	if( h->commands_outstanding >= h->max_commands)
		return(1);
	else 
		return(0);

}
/* 
 *   returns value read from hardware. 
 *     returns FIFO_EMPTY if there is nothing to read 
 */ 
static unsigned long SA5_completed(ctlr_info_t *h)
{
	unsigned long register_value 
		= readl(h->vaddr + SA5_REPLY_PORT_OFFSET);
	if(register_value != FIFO_EMPTY)
	{
		h->commands_outstanding--;
#ifdef CCISS_DEBUG
		printk("cciss:  Read %lx back from board\n", register_value);
#endif /* CCISS_DEBUG */ 
	} 
#ifdef CCISS_DEBUG
	else
	{
		printk("cciss:  FIFO Empty read\n");
	}
#endif 
	return ( register_value); 

}

/* Performant mode command completed */
static unsigned long SA5_performant_completed(ctlr_info_t *h)
{
	unsigned long register_value = FIFO_EMPTY;

	/* flush the controller write of the reply queue by reading
	 * outbound doorbell status register.
	 */
	register_value = readl(h->vaddr + SA5_OUTDB_STATUS);
	/* msi auto clears the interrupt pending bit. */
	if (!(h->msi_vector || h->msix_vector)) {
		writel(SA5_OUTDB_CLEAR_PERF_BIT, h->vaddr + SA5_OUTDB_CLEAR);
		/* Do a read in order to flush the write to the controller
		 * (as per spec.)
		 */
		register_value = readl(h->vaddr + SA5_OUTDB_STATUS);
	}

	if ((*(h->reply_pool_head) & 1) == (h->reply_pool_wraparound)) {
		register_value = *(h->reply_pool_head);
		(h->reply_pool_head)++;
		h->commands_outstanding--;
	} else {
		register_value = FIFO_EMPTY;
	}
	/* Check for wraparound */
	if (h->reply_pool_head == (h->reply_pool + h->max_commands)) {
		h->reply_pool_head = h->reply_pool;
		h->reply_pool_wraparound ^= 1;
	}

	return register_value;
}
/*
 *	Returns true if an interrupt is pending.. 
 */
static bool SA5_intr_pending(ctlr_info_t *h)
{
	unsigned long register_value  = 
		readl(h->vaddr + SA5_INTR_STATUS);
#ifdef CCISS_DEBUG
	printk("cciss: intr_pending %lx\n", register_value);
#endif  /* CCISS_DEBUG */
	if( register_value &  SA5_INTR_PENDING) 
		return  1;	
	return 0 ;
}

/*
 *      Returns true if an interrupt is pending..
 */
static bool SA5B_intr_pending(ctlr_info_t *h)
{
        unsigned long register_value  =
                readl(h->vaddr + SA5_INTR_STATUS);
#ifdef CCISS_DEBUG
        printk("cciss: intr_pending %lx\n", register_value);
#endif  /* CCISS_DEBUG */
        if( register_value &  SA5B_INTR_PENDING)
                return  1;
        return 0 ;
}

static bool SA5_performant_intr_pending(ctlr_info_t *h)
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

static struct access_method SA5B_access = {
        SA5_submit_command,
        SA5B_intr_mask,
        SA5_fifo_full,
        SA5B_intr_pending,
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
	__u32	board_id;
	char	*product_name;
	struct access_method *access;
	int nr_cmds; /* Max cmds this kind of ctlr can handle. */
};

#endif /* CCISS_H */
