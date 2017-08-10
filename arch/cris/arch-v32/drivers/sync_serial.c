/*
 * Simple synchronous serial port driver for ETRAX FS and ARTPEC-3.
 *
 * Copyright (c) 2005, 2008 Axis Communications AB
 * Author: Mikael Starvik
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/major.h>
#include <linux/sched/signal.h>
#include <linux/mutex.h>
#include <linux/interrupt.h>
#include <linux/poll.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/wait.h>

#include <asm/io.h>
#include <mach/dma.h>
#include <pinmux.h>
#include <hwregs/reg_rdwr.h>
#include <hwregs/sser_defs.h>
#include <hwregs/timer_defs.h>
#include <hwregs/dma_defs.h>
#include <hwregs/dma.h>
#include <hwregs/intr_vect_defs.h>
#include <hwregs/intr_vect.h>
#include <hwregs/reg_map.h>
#include <asm/sync_serial.h>


/* The receiver is a bit tricky because of the continuous stream of data.*/
/*                                                                       */
/* Three DMA descriptors are linked together. Each DMA descriptor is     */
/* responsible for port->bufchunk of a common buffer.                    */
/*                                                                       */
/* +---------------------------------------------+                       */
/* |   +----------+   +----------+   +----------+ |                      */
/* +-> | Descr[0] |-->| Descr[1] |-->| Descr[2] |-+                      */
/*     +----------+   +----------+   +----------+                        */
/*         |            |              |                                 */
/*         v            v              v                                 */
/*   +-------------------------------------+                             */
/*   |        BUFFER                       |                             */
/*   +-------------------------------------+                             */
/*      |<- data_avail ->|                                               */
/*    readp          writep                                              */
/*                                                                       */
/* If the application keeps up the pace readp will be right after writep.*/
/* If the application can't keep the pace we have to throw away data.    */
/* The idea is that readp should be ready with the data pointed out by	 */
/* Descr[i] when the DMA has filled in Descr[i+1].                       */
/* Otherwise we will discard	                                         */
/* the rest of the data pointed out by Descr1 and set readp to the start */
/* of Descr2                                                             */

/* IN_BUFFER_SIZE should be a multiple of 6 to make sure that 24 bit */
/* words can be handled */
#define IN_DESCR_SIZE SSP_INPUT_CHUNK_SIZE
#define NBR_IN_DESCR (8*6)
#define IN_BUFFER_SIZE (IN_DESCR_SIZE * NBR_IN_DESCR)

#define NBR_OUT_DESCR 8
#define OUT_BUFFER_SIZE (1024 * NBR_OUT_DESCR)

#define DEFAULT_FRAME_RATE 0
#define DEFAULT_WORD_RATE 7

/* To be removed when we move to pure udev. */
#define SYNC_SERIAL_MAJOR 125

/* NOTE: Enabling some debug will likely cause overrun or underrun,
 * especially if manual mode is used.
 */
#define DEBUG(x)
#define DEBUGREAD(x)
#define DEBUGWRITE(x)
#define DEBUGPOLL(x)
#define DEBUGRXINT(x)
#define DEBUGTXINT(x)
#define DEBUGTRDMA(x)
#define DEBUGOUTBUF(x)

enum syncser_irq_setup {
	no_irq_setup = 0,
	dma_irq_setup = 1,
	manual_irq_setup = 2,
};

struct sync_port {
	unsigned long regi_sser;
	unsigned long regi_dmain;
	unsigned long regi_dmaout;

	/* Interrupt vectors. */
	unsigned long dma_in_intr_vect; /* Used for DMA in. */
	unsigned long dma_out_intr_vect; /* Used for DMA out. */
	unsigned long syncser_intr_vect; /* Used when no DMA. */

	/* DMA number for in and out. */
	unsigned int dma_in_nbr;
	unsigned int dma_out_nbr;

	/* DMA owner. */
	enum dma_owner req_dma;

	char started; /* 1 if port has been started */
	char port_nbr; /* Port 0 or 1 */
	char busy; /* 1 if port is busy */

	char enabled;  /* 1 if port is enabled */
	char use_dma;  /* 1 if port uses dma */
	char tr_running;

	enum syncser_irq_setup init_irqs;
	int output;
	int input;

	/* Next byte to be read by application */
	unsigned char *readp;
	/* Next byte to be written by etrax */
	unsigned char *writep;

	unsigned int in_buffer_size;
	unsigned int in_buffer_len;
	unsigned int inbufchunk;
	/* Data buffers for in and output. */
	unsigned char out_buffer[OUT_BUFFER_SIZE] __aligned(32);
	unsigned char in_buffer[IN_BUFFER_SIZE] __aligned(32);
	unsigned char flip[IN_BUFFER_SIZE] __aligned(32);
	struct timespec timestamp[NBR_IN_DESCR];
	struct dma_descr_data *next_rx_desc;
	struct dma_descr_data *prev_rx_desc;

	struct timeval last_timestamp;
	int read_ts_idx;
	int write_ts_idx;

	/* Pointer to the first available descriptor in the ring,
	 * unless active_tr_descr == catch_tr_descr and a dma
	 * transfer is active */
	struct dma_descr_data *active_tr_descr;

	/* Pointer to the first allocated descriptor in the ring */
	struct dma_descr_data *catch_tr_descr;

	/* Pointer to the descriptor with the current end-of-list */
	struct dma_descr_data *prev_tr_descr;
	int full;

	/* Pointer to the first byte being read by DMA
	 * or current position in out_buffer if not using DMA. */
	unsigned char *out_rd_ptr;

	/* Number of bytes currently locked for being read by DMA */
	int out_buf_count;

	dma_descr_context in_context __aligned(32);
	dma_descr_context out_context __aligned(32);
	dma_descr_data in_descr[NBR_IN_DESCR] __aligned(16);
	dma_descr_data out_descr[NBR_OUT_DESCR] __aligned(16);

	wait_queue_head_t out_wait_q;
	wait_queue_head_t in_wait_q;

	spinlock_t lock;
};

static DEFINE_MUTEX(sync_serial_mutex);
static int etrax_sync_serial_init(void);
static void initialize_port(int portnbr);
static inline int sync_data_avail(struct sync_port *port);

static int sync_serial_open(struct inode *, struct file *);
static int sync_serial_release(struct inode *, struct file *);
static unsigned int sync_serial_poll(struct file *filp, poll_table *wait);

static long sync_serial_ioctl(struct file *file,
			      unsigned int cmd, unsigned long arg);
static int sync_serial_ioctl_unlocked(struct file *file,
				      unsigned int cmd, unsigned long arg);
static ssize_t sync_serial_write(struct file *file, const char __user *buf,
				 size_t count, loff_t *ppos);
static ssize_t sync_serial_read(struct file *file, char __user *buf,
				size_t count, loff_t *ppos);

#if ((defined(CONFIG_ETRAX_SYNCHRONOUS_SERIAL_PORT0) && \
	defined(CONFIG_ETRAX_SYNCHRONOUS_SERIAL0_DMA)) || \
	(defined(CONFIG_ETRAX_SYNCHRONOUS_SERIAL_PORT1) && \
	defined(CONFIG_ETRAX_SYNCHRONOUS_SERIAL1_DMA)))
#define SYNC_SER_DMA
#else
#define SYNC_SER_MANUAL
#endif

#ifdef SYNC_SER_DMA
static void start_dma_out(struct sync_port *port, const char *data, int count);
static void start_dma_in(struct sync_port *port);
static irqreturn_t tr_interrupt(int irq, void *dev_id);
static irqreturn_t rx_interrupt(int irq, void *dev_id);
#endif
#ifdef SYNC_SER_MANUAL
static void send_word(struct sync_port *port);
static irqreturn_t manual_interrupt(int irq, void *dev_id);
#endif

#define artpec_pinmux_alloc_fixed crisv32_pinmux_alloc_fixed
#define artpec_request_dma crisv32_request_dma
#define artpec_free_dma crisv32_free_dma

#ifdef CONFIG_ETRAXFS
/* ETRAX FS */
#define DMA_OUT_NBR0		SYNC_SER0_TX_DMA_NBR
#define DMA_IN_NBR0		SYNC_SER0_RX_DMA_NBR
#define DMA_OUT_NBR1		SYNC_SER1_TX_DMA_NBR
#define DMA_IN_NBR1		SYNC_SER1_RX_DMA_NBR
#define PINMUX_SSER0		pinmux_sser0
#define PINMUX_SSER1		pinmux_sser1
#define SYNCSER_INST0		regi_sser0
#define SYNCSER_INST1		regi_sser1
#define SYNCSER_INTR_VECT0	SSER0_INTR_VECT
#define SYNCSER_INTR_VECT1	SSER1_INTR_VECT
#define OUT_DMA_INST0		regi_dma4
#define IN_DMA_INST0		regi_dma5
#define DMA_OUT_INTR_VECT0	DMA4_INTR_VECT
#define DMA_OUT_INTR_VECT1	DMA7_INTR_VECT
#define DMA_IN_INTR_VECT0	DMA5_INTR_VECT
#define DMA_IN_INTR_VECT1	DMA6_INTR_VECT
#define REQ_DMA_SYNCSER0	dma_sser0
#define REQ_DMA_SYNCSER1	dma_sser1
#if defined(CONFIG_ETRAX_SYNCHRONOUS_SERIAL1_DMA)
#define PORT1_DMA 1
#else
#define PORT1_DMA 0
#endif
#elif defined(CONFIG_CRIS_MACH_ARTPEC3)
/* ARTPEC-3 */
#define DMA_OUT_NBR0		SYNC_SER_TX_DMA_NBR
#define DMA_IN_NBR0		SYNC_SER_RX_DMA_NBR
#define PINMUX_SSER0		pinmux_sser
#define SYNCSER_INST0		regi_sser
#define SYNCSER_INTR_VECT0	SSER_INTR_VECT
#define OUT_DMA_INST0		regi_dma6
#define IN_DMA_INST0		regi_dma7
#define DMA_OUT_INTR_VECT0	DMA6_INTR_VECT
#define DMA_IN_INTR_VECT0	DMA7_INTR_VECT
#define REQ_DMA_SYNCSER0	dma_sser
#define REQ_DMA_SYNCSER1	dma_sser
#endif

#if defined(CONFIG_ETRAX_SYNCHRONOUS_SERIAL0_DMA)
#define PORT0_DMA 1
#else
#define PORT0_DMA 0
#endif

/* The ports */
static struct sync_port ports[] = {
	{
		.regi_sser		= SYNCSER_INST0,
		.regi_dmaout		= OUT_DMA_INST0,
		.regi_dmain		= IN_DMA_INST0,
		.use_dma		= PORT0_DMA,
		.dma_in_intr_vect	= DMA_IN_INTR_VECT0,
		.dma_out_intr_vect	= DMA_OUT_INTR_VECT0,
		.dma_in_nbr		= DMA_IN_NBR0,
		.dma_out_nbr		= DMA_OUT_NBR0,
		.req_dma		= REQ_DMA_SYNCSER0,
		.syncser_intr_vect	= SYNCSER_INTR_VECT0,
	},
#ifdef CONFIG_ETRAXFS
	{
		.regi_sser		= SYNCSER_INST1,
		.regi_dmaout		= regi_dma6,
		.regi_dmain		= regi_dma7,
		.use_dma		= PORT1_DMA,
		.dma_in_intr_vect	= DMA_IN_INTR_VECT1,
		.dma_out_intr_vect	= DMA_OUT_INTR_VECT1,
		.dma_in_nbr		= DMA_IN_NBR1,
		.dma_out_nbr		= DMA_OUT_NBR1,
		.req_dma		= REQ_DMA_SYNCSER1,
		.syncser_intr_vect	= SYNCSER_INTR_VECT1,
	},
#endif
};

#define NBR_PORTS ARRAY_SIZE(ports)

static const struct file_operations syncser_fops = {
	.owner		= THIS_MODULE,
	.write		= sync_serial_write,
	.read		= sync_serial_read,
	.poll		= sync_serial_poll,
	.unlocked_ioctl	= sync_serial_ioctl,
	.open		= sync_serial_open,
	.release	= sync_serial_release,
	.llseek		= noop_llseek,
};

static dev_t syncser_first;
static int minor_count = NBR_PORTS;
#define SYNCSER_NAME "syncser"
static struct cdev *syncser_cdev;
static struct class *syncser_class;

static void sync_serial_start_port(struct sync_port *port)
{
	reg_sser_rw_cfg cfg = REG_RD(sser, port->regi_sser, rw_cfg);
	reg_sser_rw_tr_cfg tr_cfg =
		REG_RD(sser, port->regi_sser, rw_tr_cfg);
	reg_sser_rw_rec_cfg rec_cfg =
		REG_RD(sser, port->regi_sser, rw_rec_cfg);
	cfg.en = regk_sser_yes;
	tr_cfg.tr_en = regk_sser_yes;
	rec_cfg.rec_en = regk_sser_yes;
	REG_WR(sser, port->regi_sser, rw_cfg, cfg);
	REG_WR(sser, port->regi_sser, rw_tr_cfg, tr_cfg);
	REG_WR(sser, port->regi_sser, rw_rec_cfg, rec_cfg);
	port->started = 1;
}

static void __init initialize_port(int portnbr)
{
	struct sync_port *port = &ports[portnbr];
	reg_sser_rw_cfg cfg = { 0 };
	reg_sser_rw_frm_cfg frm_cfg = { 0 };
	reg_sser_rw_tr_cfg tr_cfg = { 0 };
	reg_sser_rw_rec_cfg rec_cfg = { 0 };

	DEBUG(pr_info("Init sync serial port %d\n", portnbr));

	port->port_nbr = portnbr;
	port->init_irqs = no_irq_setup;

	port->out_rd_ptr = port->out_buffer;
	port->out_buf_count = 0;

	port->output = 1;
	port->input = 0;

	port->readp = port->flip;
	port->writep = port->flip;
	port->in_buffer_size = IN_BUFFER_SIZE;
	port->in_buffer_len = 0;
	port->inbufchunk = IN_DESCR_SIZE;

	port->read_ts_idx = 0;
	port->write_ts_idx = 0;

	init_waitqueue_head(&port->out_wait_q);
	init_waitqueue_head(&port->in_wait_q);

	spin_lock_init(&port->lock);

	cfg.out_clk_src = regk_sser_intern_clk;
	cfg.out_clk_pol = regk_sser_pos;
	cfg.clk_od_mode = regk_sser_no;
	cfg.clk_dir = regk_sser_out;
	cfg.gate_clk = regk_sser_no;
	cfg.base_freq = regk_sser_f29_493;
	cfg.clk_div = 256;
	REG_WR(sser, port->regi_sser, rw_cfg, cfg);

	frm_cfg.wordrate = DEFAULT_WORD_RATE;
	frm_cfg.type = regk_sser_edge;
	frm_cfg.frame_pin_dir = regk_sser_out;
	frm_cfg.frame_pin_use = regk_sser_frm;
	frm_cfg.status_pin_dir = regk_sser_in;
	frm_cfg.status_pin_use = regk_sser_hold;
	frm_cfg.out_on = regk_sser_tr;
	frm_cfg.tr_delay = 1;
	REG_WR(sser, port->regi_sser, rw_frm_cfg, frm_cfg);

	tr_cfg.urun_stop = regk_sser_no;
	tr_cfg.sample_size = 7;
	tr_cfg.sh_dir = regk_sser_msbfirst;
	tr_cfg.use_dma = port->use_dma ? regk_sser_yes : regk_sser_no;
#if 0
	tr_cfg.rate_ctrl = regk_sser_bulk;
	tr_cfg.data_pin_use = regk_sser_dout;
#else
	tr_cfg.rate_ctrl = regk_sser_iso;
	tr_cfg.data_pin_use = regk_sser_dout;
#endif
	tr_cfg.bulk_wspace = 1;
	REG_WR(sser, port->regi_sser, rw_tr_cfg, tr_cfg);

	rec_cfg.sample_size = 7;
	rec_cfg.sh_dir = regk_sser_msbfirst;
	rec_cfg.use_dma = port->use_dma ? regk_sser_yes : regk_sser_no;
	rec_cfg.fifo_thr = regk_sser_inf;
	REG_WR(sser, port->regi_sser, rw_rec_cfg, rec_cfg);

#ifdef SYNC_SER_DMA
	{
		int i;
		/* Setup the descriptor ring for dma out/transmit. */
		for (i = 0; i < NBR_OUT_DESCR; i++) {
			dma_descr_data *descr = &port->out_descr[i];
			descr->wait = 0;
			descr->intr = 1;
			descr->eol = 0;
			descr->out_eop = 0;
			descr->next =
				(dma_descr_data *)virt_to_phys(&descr[i+1]);
		}
	}

	/* Create a ring from the list. */
	port->out_descr[NBR_OUT_DESCR-1].next =
		(dma_descr_data *)virt_to_phys(&port->out_descr[0]);

	/* Setup context for traversing the ring. */
	port->active_tr_descr = &port->out_descr[0];
	port->prev_tr_descr = &port->out_descr[NBR_OUT_DESCR-1];
	port->catch_tr_descr = &port->out_descr[0];
#endif
}

static inline int sync_data_avail(struct sync_port *port)
{
	return port->in_buffer_len;
}

static int sync_serial_open(struct inode *inode, struct file *file)
{
	int ret = 0;
	int dev = iminor(inode);
	struct sync_port *port;
#ifdef SYNC_SER_DMA
	reg_dma_rw_cfg cfg = { .en = regk_dma_yes };
	reg_dma_rw_intr_mask intr_mask = { .data = regk_dma_yes };
#endif

	DEBUG(pr_debug("Open sync serial port %d\n", dev));

	if (dev < 0 || dev >= NBR_PORTS || !ports[dev].enabled) {
		DEBUG(pr_info("Invalid minor %d\n", dev));
		return -ENODEV;
	}
	port = &ports[dev];
	/* Allow open this device twice (assuming one reader and one writer) */
	if (port->busy == 2) {
		DEBUG(pr_info("syncser%d is busy\n", dev));
		return -EBUSY;
	}

	mutex_lock(&sync_serial_mutex);

	/* Clear any stale date left in the flip buffer */
	port->readp = port->writep = port->flip;
	port->in_buffer_len = 0;
	port->read_ts_idx = 0;
	port->write_ts_idx = 0;

	if (port->init_irqs != no_irq_setup) {
		/* Init only on first call. */
		port->busy++;
		mutex_unlock(&sync_serial_mutex);
		return 0;
	}
	if (port->use_dma) {
#ifdef SYNC_SER_DMA
		const char *tmp;
		DEBUG(pr_info("Using DMA for syncser%d\n", dev));

		tmp = dev == 0 ? "syncser0 tx" : "syncser1 tx";
		if (request_irq(port->dma_out_intr_vect, tr_interrupt, 0,
				tmp, port)) {
			pr_err("Can't alloc syncser%d TX IRQ", dev);
			ret = -EBUSY;
			goto unlock_and_exit;
		}
		if (artpec_request_dma(port->dma_out_nbr, tmp,
				DMA_VERBOSE_ON_ERROR, 0, port->req_dma)) {
			free_irq(port->dma_out_intr_vect, port);
			pr_err("Can't alloc syncser%d TX DMA", dev);
			ret = -EBUSY;
			goto unlock_and_exit;
		}
		tmp = dev == 0 ? "syncser0 rx" : "syncser1 rx";
		if (request_irq(port->dma_in_intr_vect, rx_interrupt, 0,
				tmp, port)) {
			artpec_free_dma(port->dma_out_nbr);
			free_irq(port->dma_out_intr_vect, port);
			pr_err("Can't alloc syncser%d RX IRQ", dev);
			ret = -EBUSY;
			goto unlock_and_exit;
		}
		if (artpec_request_dma(port->dma_in_nbr, tmp,
				DMA_VERBOSE_ON_ERROR, 0, port->req_dma)) {
			artpec_free_dma(port->dma_out_nbr);
			free_irq(port->dma_out_intr_vect, port);
			free_irq(port->dma_in_intr_vect, port);
			pr_err("Can't alloc syncser%d RX DMA", dev);
			ret = -EBUSY;
			goto unlock_and_exit;
		}
		/* Enable DMAs */
		REG_WR(dma, port->regi_dmain, rw_cfg, cfg);
		REG_WR(dma, port->regi_dmaout, rw_cfg, cfg);
		/* Enable DMA IRQs */
		REG_WR(dma, port->regi_dmain, rw_intr_mask, intr_mask);
		REG_WR(dma, port->regi_dmaout, rw_intr_mask, intr_mask);
		/* Set up wordsize = 1 for DMAs. */
		DMA_WR_CMD(port->regi_dmain, regk_dma_set_w_size1);
		DMA_WR_CMD(port->regi_dmaout, regk_dma_set_w_size1);

		start_dma_in(port);
		port->init_irqs = dma_irq_setup;
#endif
	} else { /* !port->use_dma */
#ifdef SYNC_SER_MANUAL
		const char *tmp = dev == 0 ? "syncser0 manual irq" :
					     "syncser1 manual irq";
		if (request_irq(port->syncser_intr_vect, manual_interrupt,
				0, tmp, port)) {
			pr_err("Can't alloc syncser%d manual irq",
				dev);
			ret = -EBUSY;
			goto unlock_and_exit;
		}
		port->init_irqs = manual_irq_setup;
#else
		panic("sync_serial: Manual mode not supported\n");
#endif /* SYNC_SER_MANUAL */
	}
	port->busy++;
	ret = 0;

unlock_and_exit:
	mutex_unlock(&sync_serial_mutex);
	return ret;
}

static int sync_serial_release(struct inode *inode, struct file *file)
{
	int dev = iminor(inode);
	struct sync_port *port;

	if (dev < 0 || dev >= NBR_PORTS || !ports[dev].enabled) {
		DEBUG(pr_info("Invalid minor %d\n", dev));
		return -ENODEV;
	}
	port = &ports[dev];
	if (port->busy)
		port->busy--;
	if (!port->busy)
		/* XXX */;
	return 0;
}

static unsigned int sync_serial_poll(struct file *file, poll_table *wait)
{
	int dev = iminor(file_inode(file));
	unsigned int mask = 0;
	struct sync_port *port;
	DEBUGPOLL(
	static unsigned int prev_mask;
	);

	port = &ports[dev];

	if (!port->started)
		sync_serial_start_port(port);

	poll_wait(file, &port->out_wait_q, wait);
	poll_wait(file, &port->in_wait_q, wait);

	/* No active transfer, descriptors are available */
	if (port->output && !port->tr_running)
		mask |= POLLOUT | POLLWRNORM;

	/* Descriptor and buffer space available. */
	if (port->output &&
	    port->active_tr_descr != port->catch_tr_descr &&
	    port->out_buf_count < OUT_BUFFER_SIZE)
		mask |=  POLLOUT | POLLWRNORM;

	/* At least an inbufchunk of data */
	if (port->input && sync_data_avail(port) >= port->inbufchunk)
		mask |= POLLIN | POLLRDNORM;

	DEBUGPOLL(
	if (mask != prev_mask)
		pr_info("sync_serial_poll: mask 0x%08X %s %s\n",
			mask,
			mask & POLLOUT ? "POLLOUT" : "",
			mask & POLLIN ? "POLLIN" : "");
		prev_mask = mask;
	);
	return mask;
}

static ssize_t __sync_serial_read(struct file *file,
				  char __user *buf,
				  size_t count,
				  loff_t *ppos,
				  struct timespec *ts)
{
	unsigned long flags;
	int dev = MINOR(file_inode(file)->i_rdev);
	int avail;
	struct sync_port *port;
	unsigned char *start;
	unsigned char *end;

	if (dev < 0 || dev >= NBR_PORTS || !ports[dev].enabled) {
		DEBUG(pr_info("Invalid minor %d\n", dev));
		return -ENODEV;
	}
	port = &ports[dev];

	if (!port->started)
		sync_serial_start_port(port);

	/* Calculate number of available bytes */
	/* Save pointers to avoid that they are modified by interrupt */
	spin_lock_irqsave(&port->lock, flags);
	start = port->readp;
	end = port->writep;
	spin_unlock_irqrestore(&port->lock, flags);

	while ((start == end) && !port->in_buffer_len) {
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;

		wait_event_interruptible(port->in_wait_q,
					 !(start == end && !port->full));

		if (signal_pending(current))
			return -EINTR;

		spin_lock_irqsave(&port->lock, flags);
		start = port->readp;
		end = port->writep;
		spin_unlock_irqrestore(&port->lock, flags);
	}

	DEBUGREAD(pr_info("R%d c %d ri %u wi %u /%u\n",
			  dev, count,
			  start - port->flip, end - port->flip,
			  port->in_buffer_size));

	/* Lazy read, never return wrapped data. */
	if (end > start)
		avail = end - start;
	else
		avail = port->flip + port->in_buffer_size - start;

	count = count > avail ? avail : count;
	if (copy_to_user(buf, start, count))
		return -EFAULT;

	/* If timestamp requested, find timestamp of first returned byte
	 * and copy it.
	 * N.B: Applications that request timstamps MUST read data in
	 * chunks that are multiples of IN_DESCR_SIZE.
	 * Otherwise the timestamps will not be aligned to the data read.
	 */
	if (ts != NULL) {
		int idx = port->read_ts_idx;
		memcpy(ts, &port->timestamp[idx], sizeof(struct timespec));
		port->read_ts_idx += count / IN_DESCR_SIZE;
		if (port->read_ts_idx >= NBR_IN_DESCR)
			port->read_ts_idx = 0;
	}

	spin_lock_irqsave(&port->lock, flags);
	port->readp += count;
	/* Check for wrap */
	if (port->readp >= port->flip + port->in_buffer_size)
		port->readp = port->flip;
	port->in_buffer_len -= count;
	port->full = 0;
	spin_unlock_irqrestore(&port->lock, flags);

	DEBUGREAD(pr_info("r %d\n", count));

	return count;
}

static ssize_t sync_serial_input(struct file *file, unsigned long arg)
{
	struct ssp_request req;
	int count;
	int ret;

	/* Copy the request structure from user-mode. */
	ret = copy_from_user(&req, (struct ssp_request __user *)arg,
		sizeof(struct ssp_request));

	if (ret) {
		DEBUG(pr_info("sync_serial_input copy from user failed\n"));
		return -EFAULT;
	}

	/* To get the timestamps aligned, make sure that 'len'
	 * is a multiple of IN_DESCR_SIZE.
	 */
	if ((req.len % IN_DESCR_SIZE) != 0) {
		DEBUG(pr_info("sync_serial: req.len %x, IN_DESCR_SIZE %x\n",
			      req.len, IN_DESCR_SIZE));
		return -EFAULT;
	}

	/* Do the actual read. */
	/* Note that req.buf is actually a pointer to user space. */
	count = __sync_serial_read(file, req.buf, req.len,
				   NULL, &req.ts);

	if (count < 0) {
		DEBUG(pr_info("sync_serial_input read failed\n"));
		return count;
	}

	/* Copy the request back to user-mode. */
	ret = copy_to_user((struct ssp_request __user *)arg, &req,
		sizeof(struct ssp_request));

	if (ret) {
		DEBUG(pr_info("syncser input copy2user failed\n"));
		return -EFAULT;
	}

	/* Return the number of bytes read. */
	return count;
}


static int sync_serial_ioctl_unlocked(struct file *file,
				      unsigned int cmd, unsigned long arg)
{
	int return_val = 0;
	int dma_w_size = regk_dma_set_w_size1;
	int dev = iminor(file_inode(file));
	struct sync_port *port;
	reg_sser_rw_tr_cfg tr_cfg;
	reg_sser_rw_rec_cfg rec_cfg;
	reg_sser_rw_frm_cfg frm_cfg;
	reg_sser_rw_cfg gen_cfg;
	reg_sser_rw_intr_mask intr_mask;

	if (dev < 0 || dev >= NBR_PORTS || !ports[dev].enabled) {
		DEBUG(pr_info("Invalid minor %d\n", dev));
		return -1;
	}

	if (cmd == SSP_INPUT)
		return sync_serial_input(file, arg);

	port = &ports[dev];
	spin_lock_irq(&port->lock);

	tr_cfg = REG_RD(sser, port->regi_sser, rw_tr_cfg);
	rec_cfg = REG_RD(sser, port->regi_sser, rw_rec_cfg);
	frm_cfg = REG_RD(sser, port->regi_sser, rw_frm_cfg);
	gen_cfg = REG_RD(sser, port->regi_sser, rw_cfg);
	intr_mask = REG_RD(sser, port->regi_sser, rw_intr_mask);

	switch (cmd) {
	case SSP_SPEED:
		if (GET_SPEED(arg) == CODEC) {
			unsigned int freq;

			gen_cfg.base_freq = regk_sser_f32;

			/* Clock divider will internally be
			 * gen_cfg.clk_div + 1.
			 */

			freq = GET_FREQ(arg);
			switch (freq) {
			case FREQ_32kHz:
			case FREQ_64kHz:
			case FREQ_128kHz:
			case FREQ_256kHz:
				gen_cfg.clk_div = 125 *
					(1 << (freq - FREQ_256kHz)) - 1;
				break;
			case FREQ_512kHz:
				gen_cfg.clk_div = 62;
				break;
			case FREQ_1MHz:
			case FREQ_2MHz:
			case FREQ_4MHz:
				gen_cfg.clk_div = 8 * (1 << freq) - 1;
				break;
			}
		} else if (GET_SPEED(arg) == CODEC_f32768) {
			gen_cfg.base_freq = regk_sser_f32_768;
			switch (GET_FREQ(arg)) {
			case FREQ_4096kHz:
				gen_cfg.clk_div = 7;
				break;
			default:
				spin_unlock_irq(&port->lock);
				return -EINVAL;
			}
		} else {
			gen_cfg.base_freq = regk_sser_f29_493;
			switch (GET_SPEED(arg)) {
			case SSP150:
				gen_cfg.clk_div = 29493000 / (150 * 8) - 1;
				break;
			case SSP300:
				gen_cfg.clk_div = 29493000 / (300 * 8) - 1;
				break;
			case SSP600:
				gen_cfg.clk_div = 29493000 / (600 * 8) - 1;
				break;
			case SSP1200:
				gen_cfg.clk_div = 29493000 / (1200 * 8) - 1;
				break;
			case SSP2400:
				gen_cfg.clk_div = 29493000 / (2400 * 8) - 1;
				break;
			case SSP4800:
				gen_cfg.clk_div = 29493000 / (4800 * 8) - 1;
				break;
			case SSP9600:
				gen_cfg.clk_div = 29493000 / (9600 * 8) - 1;
				break;
			case SSP19200:
				gen_cfg.clk_div = 29493000 / (19200 * 8) - 1;
				break;
			case SSP28800:
				gen_cfg.clk_div = 29493000 / (28800 * 8) - 1;
				break;
			case SSP57600:
				gen_cfg.clk_div = 29493000 / (57600 * 8) - 1;
				break;
			case SSP115200:
				gen_cfg.clk_div = 29493000 / (115200 * 8) - 1;
				break;
			case SSP230400:
				gen_cfg.clk_div = 29493000 / (230400 * 8) - 1;
				break;
			case SSP460800:
				gen_cfg.clk_div = 29493000 / (460800 * 8) - 1;
				break;
			case SSP921600:
				gen_cfg.clk_div = 29493000 / (921600 * 8) - 1;
				break;
			case SSP3125000:
				gen_cfg.base_freq = regk_sser_f100;
				gen_cfg.clk_div = 100000000 / (3125000 * 8) - 1;
				break;

			}
		}
		frm_cfg.wordrate = GET_WORD_RATE(arg);

		break;
	case SSP_MODE:
		switch (arg) {
		case MASTER_OUTPUT:
			port->output = 1;
			port->input = 0;
			frm_cfg.out_on = regk_sser_tr;
			frm_cfg.frame_pin_dir = regk_sser_out;
			gen_cfg.clk_dir = regk_sser_out;
			break;
		case SLAVE_OUTPUT:
			port->output = 1;
			port->input = 0;
			frm_cfg.frame_pin_dir = regk_sser_in;
			gen_cfg.clk_dir = regk_sser_in;
			break;
		case MASTER_INPUT:
			port->output = 0;
			port->input = 1;
			frm_cfg.frame_pin_dir = regk_sser_out;
			frm_cfg.out_on = regk_sser_intern_tb;
			gen_cfg.clk_dir = regk_sser_out;
			break;
		case SLAVE_INPUT:
			port->output = 0;
			port->input = 1;
			frm_cfg.frame_pin_dir = regk_sser_in;
			gen_cfg.clk_dir = regk_sser_in;
			break;
		case MASTER_BIDIR:
			port->output = 1;
			port->input = 1;
			frm_cfg.frame_pin_dir = regk_sser_out;
			frm_cfg.out_on = regk_sser_intern_tb;
			gen_cfg.clk_dir = regk_sser_out;
			break;
		case SLAVE_BIDIR:
			port->output = 1;
			port->input = 1;
			frm_cfg.frame_pin_dir = regk_sser_in;
			gen_cfg.clk_dir = regk_sser_in;
			break;
		default:
			spin_unlock_irq(&port->lock);
			return -EINVAL;
		}
		if (!port->use_dma || arg == MASTER_OUTPUT ||
				arg == SLAVE_OUTPUT)
			intr_mask.rdav = regk_sser_yes;
		break;
	case SSP_FRAME_SYNC:
		if (arg & NORMAL_SYNC) {
			frm_cfg.rec_delay = 1;
			frm_cfg.tr_delay = 1;
		} else if (arg & EARLY_SYNC)
			frm_cfg.rec_delay = frm_cfg.tr_delay = 0;
		else if (arg & LATE_SYNC) {
			frm_cfg.tr_delay = 2;
			frm_cfg.rec_delay = 2;
		} else if (arg & SECOND_WORD_SYNC) {
			frm_cfg.rec_delay = 7;
			frm_cfg.tr_delay = 1;
		}

		tr_cfg.bulk_wspace = frm_cfg.tr_delay;
		frm_cfg.early_wend = regk_sser_yes;
		if (arg & BIT_SYNC)
			frm_cfg.type = regk_sser_edge;
		else if (arg & WORD_SYNC)
			frm_cfg.type = regk_sser_level;
		else if (arg & EXTENDED_SYNC)
			frm_cfg.early_wend = regk_sser_no;

		if (arg & SYNC_ON)
			frm_cfg.frame_pin_use = regk_sser_frm;
		else if (arg & SYNC_OFF)
			frm_cfg.frame_pin_use = regk_sser_gio0;

		dma_w_size = regk_dma_set_w_size2;
		if (arg & WORD_SIZE_8) {
			rec_cfg.sample_size = tr_cfg.sample_size = 7;
			dma_w_size = regk_dma_set_w_size1;
		} else if (arg & WORD_SIZE_12)
			rec_cfg.sample_size = tr_cfg.sample_size = 11;
		else if (arg & WORD_SIZE_16)
			rec_cfg.sample_size = tr_cfg.sample_size = 15;
		else if (arg & WORD_SIZE_24)
			rec_cfg.sample_size = tr_cfg.sample_size = 23;
		else if (arg & WORD_SIZE_32)
			rec_cfg.sample_size = tr_cfg.sample_size = 31;

		if (arg & BIT_ORDER_MSB)
			rec_cfg.sh_dir = tr_cfg.sh_dir = regk_sser_msbfirst;
		else if (arg & BIT_ORDER_LSB)
			rec_cfg.sh_dir = tr_cfg.sh_dir = regk_sser_lsbfirst;

		if (arg & FLOW_CONTROL_ENABLE) {
			frm_cfg.status_pin_use = regk_sser_frm;
			rec_cfg.fifo_thr = regk_sser_thr16;
		} else if (arg & FLOW_CONTROL_DISABLE) {
			frm_cfg.status_pin_use = regk_sser_gio0;
			rec_cfg.fifo_thr = regk_sser_inf;
		}

		if (arg & CLOCK_NOT_GATED)
			gen_cfg.gate_clk = regk_sser_no;
		else if (arg & CLOCK_GATED)
			gen_cfg.gate_clk = regk_sser_yes;

		break;
	case SSP_IPOLARITY:
		/* NOTE!! negedge is considered NORMAL */
		if (arg & CLOCK_NORMAL)
			rec_cfg.clk_pol = regk_sser_neg;
		else if (arg & CLOCK_INVERT)
			rec_cfg.clk_pol = regk_sser_pos;

		if (arg & FRAME_NORMAL)
			frm_cfg.level = regk_sser_pos_hi;
		else if (arg & FRAME_INVERT)
			frm_cfg.level = regk_sser_neg_lo;

		if (arg & STATUS_NORMAL)
			gen_cfg.hold_pol = regk_sser_pos;
		else if (arg & STATUS_INVERT)
			gen_cfg.hold_pol = regk_sser_neg;
		break;
	case SSP_OPOLARITY:
		if (arg & CLOCK_NORMAL)
			gen_cfg.out_clk_pol = regk_sser_pos;
		else if (arg & CLOCK_INVERT)
			gen_cfg.out_clk_pol = regk_sser_neg;

		if (arg & FRAME_NORMAL)
			frm_cfg.level = regk_sser_pos_hi;
		else if (arg & FRAME_INVERT)
			frm_cfg.level = regk_sser_neg_lo;

		if (arg & STATUS_NORMAL)
			gen_cfg.hold_pol = regk_sser_pos;
		else if (arg & STATUS_INVERT)
			gen_cfg.hold_pol = regk_sser_neg;
		break;
	case SSP_SPI:
		rec_cfg.fifo_thr = regk_sser_inf;
		rec_cfg.sh_dir = tr_cfg.sh_dir = regk_sser_msbfirst;
		rec_cfg.sample_size = tr_cfg.sample_size = 7;
		frm_cfg.frame_pin_use = regk_sser_frm;
		frm_cfg.type = regk_sser_level;
		frm_cfg.tr_delay = 1;
		frm_cfg.level = regk_sser_neg_lo;
		if (arg & SPI_SLAVE) {
			rec_cfg.clk_pol = regk_sser_neg;
			gen_cfg.clk_dir = regk_sser_in;
			port->input = 1;
			port->output = 0;
		} else {
			gen_cfg.out_clk_pol = regk_sser_pos;
			port->input = 0;
			port->output = 1;
			gen_cfg.clk_dir = regk_sser_out;
		}
		break;
	case SSP_INBUFCHUNK:
		break;
	default:
		return_val = -1;
	}


	if (port->started) {
		rec_cfg.rec_en = port->input;
		gen_cfg.en = (port->output | port->input);
	}

	REG_WR(sser, port->regi_sser, rw_tr_cfg, tr_cfg);
	REG_WR(sser, port->regi_sser, rw_rec_cfg, rec_cfg);
	REG_WR(sser, port->regi_sser, rw_frm_cfg, frm_cfg);
	REG_WR(sser, port->regi_sser, rw_intr_mask, intr_mask);
	REG_WR(sser, port->regi_sser, rw_cfg, gen_cfg);


	if (cmd == SSP_FRAME_SYNC && (arg & (WORD_SIZE_8 | WORD_SIZE_12 |
			WORD_SIZE_16 | WORD_SIZE_24 | WORD_SIZE_32))) {
		int en = gen_cfg.en;
		gen_cfg.en = 0;
		REG_WR(sser, port->regi_sser, rw_cfg, gen_cfg);
		/* ##### Should DMA be stoped before we change dma size? */
		DMA_WR_CMD(port->regi_dmain, dma_w_size);
		DMA_WR_CMD(port->regi_dmaout, dma_w_size);
		gen_cfg.en = en;
		REG_WR(sser, port->regi_sser, rw_cfg, gen_cfg);
	}

	spin_unlock_irq(&port->lock);
	return return_val;
}

static long sync_serial_ioctl(struct file *file,
		unsigned int cmd, unsigned long arg)
{
	long ret;

	mutex_lock(&sync_serial_mutex);
	ret = sync_serial_ioctl_unlocked(file, cmd, arg);
	mutex_unlock(&sync_serial_mutex);

	return ret;
}

/* NOTE: sync_serial_write does not support concurrency */
static ssize_t sync_serial_write(struct file *file, const char __user *buf,
				 size_t count, loff_t *ppos)
{
	int dev = iminor(file_inode(file));
	DECLARE_WAITQUEUE(wait, current);
	struct sync_port *port;
	int trunc_count;
	unsigned long flags;
	int bytes_free;
	int out_buf_count;

	unsigned char *rd_ptr;       /* First allocated byte in the buffer */
	unsigned char *wr_ptr;       /* First free byte in the buffer */
	unsigned char *buf_stop_ptr; /* Last byte + 1 */

	if (dev < 0 || dev >= NBR_PORTS || !ports[dev].enabled) {
		DEBUG(pr_info("Invalid minor %d\n", dev));
		return -ENODEV;
	}
	port = &ports[dev];

	/* |<-         OUT_BUFFER_SIZE                          ->|
	 *           |<- out_buf_count ->|
	 *                               |<- trunc_count ->| ...->|
	 *  ______________________________________________________
	 * |  free   |   data            | free                   |
	 * |_________|___________________|________________________|
	 *           ^ rd_ptr            ^ wr_ptr
	 */
	DEBUGWRITE(pr_info("W d%d c %u a: %p c: %p\n",
			   port->port_nbr, count, port->active_tr_descr,
			   port->catch_tr_descr));

	/* Read variables that may be updated by interrupts */
	spin_lock_irqsave(&port->lock, flags);
	rd_ptr = port->out_rd_ptr;
	out_buf_count = port->out_buf_count;
	spin_unlock_irqrestore(&port->lock, flags);

	/* Check if resources are available */
	if (port->tr_running &&
	    ((port->use_dma && port->active_tr_descr == port->catch_tr_descr) ||
	     out_buf_count >= OUT_BUFFER_SIZE)) {
		DEBUGWRITE(pr_info("sser%d full\n", dev));
		return -EAGAIN;
	}

	buf_stop_ptr = port->out_buffer + OUT_BUFFER_SIZE;

	/* Determine pointer to the first free byte, before copying. */
	wr_ptr = rd_ptr + out_buf_count;
	if (wr_ptr >= buf_stop_ptr)
		wr_ptr -= OUT_BUFFER_SIZE;

	/* If we wrap the ring buffer, let the user space program handle it by
	 * truncating the data. This could be more elegant, small buffer
	 * fragments may occur.
	 */
	bytes_free = OUT_BUFFER_SIZE - out_buf_count;
	if (wr_ptr + bytes_free > buf_stop_ptr)
		bytes_free = buf_stop_ptr - wr_ptr;
	trunc_count = (count < bytes_free) ? count : bytes_free;

	if (copy_from_user(wr_ptr, buf, trunc_count))
		return -EFAULT;

	DEBUGOUTBUF(pr_info("%-4d + %-4d = %-4d     %p %p %p\n",
			    out_buf_count, trunc_count,
			    port->out_buf_count, port->out_buffer,
			    wr_ptr, buf_stop_ptr));

	/* Make sure transmitter/receiver is running */
	if (!port->started) {
		reg_sser_rw_cfg cfg = REG_RD(sser, port->regi_sser, rw_cfg);
		reg_sser_rw_rec_cfg rec_cfg =
			REG_RD(sser, port->regi_sser, rw_rec_cfg);
		cfg.en = regk_sser_yes;
		rec_cfg.rec_en = port->input;
		REG_WR(sser, port->regi_sser, rw_cfg, cfg);
		REG_WR(sser, port->regi_sser, rw_rec_cfg, rec_cfg);
		port->started = 1;
	}

	/* Setup wait if blocking */
	if (!(file->f_flags & O_NONBLOCK)) {
		add_wait_queue(&port->out_wait_q, &wait);
		set_current_state(TASK_INTERRUPTIBLE);
	}

	spin_lock_irqsave(&port->lock, flags);
	port->out_buf_count += trunc_count;
	if (port->use_dma) {
#ifdef SYNC_SER_DMA
		start_dma_out(port, wr_ptr, trunc_count);
#endif
	} else if (!port->tr_running) {
#ifdef SYNC_SER_MANUAL
		reg_sser_rw_intr_mask intr_mask;
		intr_mask = REG_RD(sser, port->regi_sser, rw_intr_mask);
		/* Start sender by writing data */
		send_word(port);
		/* and enable transmitter ready IRQ */
		intr_mask.trdy = 1;
		REG_WR(sser, port->regi_sser, rw_intr_mask, intr_mask);
#endif
	}
	spin_unlock_irqrestore(&port->lock, flags);

	/* Exit if non blocking */
	if (file->f_flags & O_NONBLOCK) {
		DEBUGWRITE(pr_info("w d%d c %u  %08x\n",
				   port->port_nbr, trunc_count,
				   REG_RD_INT(dma, port->regi_dmaout, r_intr)));
		return trunc_count;
	}

	schedule();
	remove_wait_queue(&port->out_wait_q, &wait);

	if (signal_pending(current))
		return -EINTR;

	DEBUGWRITE(pr_info("w d%d c %u\n", port->port_nbr, trunc_count));
	return trunc_count;
}

static ssize_t sync_serial_read(struct file *file, char __user *buf,
				size_t count, loff_t *ppos)
{
	return __sync_serial_read(file, buf, count, ppos, NULL);
}

#ifdef SYNC_SER_MANUAL
static void send_word(struct sync_port *port)
{
	reg_sser_rw_tr_cfg tr_cfg = REG_RD(sser, port->regi_sser, rw_tr_cfg);
	reg_sser_rw_tr_data tr_data =  {0};

	switch (tr_cfg.sample_size) {
	case 8:
		port->out_buf_count--;
		tr_data.data = *port->out_rd_ptr++;
		REG_WR(sser, port->regi_sser, rw_tr_data, tr_data);
		if (port->out_rd_ptr >= port->out_buffer + OUT_BUFFER_SIZE)
			port->out_rd_ptr = port->out_buffer;
		break;
	case 12:
	{
		int data = (*port->out_rd_ptr++) << 8;
		data |= *port->out_rd_ptr++;
		port->out_buf_count -= 2;
		tr_data.data = data;
		REG_WR(sser, port->regi_sser, rw_tr_data, tr_data);
		if (port->out_rd_ptr >= port->out_buffer + OUT_BUFFER_SIZE)
			port->out_rd_ptr = port->out_buffer;
		break;
	}
	case 16:
		port->out_buf_count -= 2;
		tr_data.data = *(unsigned short *)port->out_rd_ptr;
		REG_WR(sser, port->regi_sser, rw_tr_data, tr_data);
		port->out_rd_ptr += 2;
		if (port->out_rd_ptr >= port->out_buffer + OUT_BUFFER_SIZE)
			port->out_rd_ptr = port->out_buffer;
		break;
	case 24:
		port->out_buf_count -= 3;
		tr_data.data = *(unsigned short *)port->out_rd_ptr;
		REG_WR(sser, port->regi_sser, rw_tr_data, tr_data);
		port->out_rd_ptr += 2;
		tr_data.data = *port->out_rd_ptr++;
		REG_WR(sser, port->regi_sser, rw_tr_data, tr_data);
		if (port->out_rd_ptr >= port->out_buffer + OUT_BUFFER_SIZE)
			port->out_rd_ptr = port->out_buffer;
		break;
	case 32:
		port->out_buf_count -= 4;
		tr_data.data = *(unsigned short *)port->out_rd_ptr;
		REG_WR(sser, port->regi_sser, rw_tr_data, tr_data);
		port->out_rd_ptr += 2;
		tr_data.data = *(unsigned short *)port->out_rd_ptr;
		REG_WR(sser, port->regi_sser, rw_tr_data, tr_data);
		port->out_rd_ptr += 2;
		if (port->out_rd_ptr >= port->out_buffer + OUT_BUFFER_SIZE)
			port->out_rd_ptr = port->out_buffer;
		break;
	}
}
#endif

#ifdef SYNC_SER_DMA
static void start_dma_out(struct sync_port *port, const char *data, int count)
{
	port->active_tr_descr->buf = (char *)virt_to_phys((char *)data);
	port->active_tr_descr->after = port->active_tr_descr->buf + count;
	port->active_tr_descr->intr = 1;

	port->active_tr_descr->eol = 1;
	port->prev_tr_descr->eol = 0;

	DEBUGTRDMA(pr_info("Inserting eolr:%p eol@:%p\n",
		port->prev_tr_descr, port->active_tr_descr));
	port->prev_tr_descr = port->active_tr_descr;
	port->active_tr_descr = phys_to_virt((int)port->active_tr_descr->next);

	if (!port->tr_running) {
		reg_sser_rw_tr_cfg tr_cfg = REG_RD(sser, port->regi_sser,
			rw_tr_cfg);

		port->out_context.next = NULL;
		port->out_context.saved_data =
			(dma_descr_data *)virt_to_phys(port->prev_tr_descr);
		port->out_context.saved_data_buf = port->prev_tr_descr->buf;

		DMA_START_CONTEXT(port->regi_dmaout,
			virt_to_phys((char *)&port->out_context));

		tr_cfg.tr_en = regk_sser_yes;
		REG_WR(sser, port->regi_sser, rw_tr_cfg, tr_cfg);
		DEBUGTRDMA(pr_info("dma s\n"););
	} else {
		DMA_CONTINUE_DATA(port->regi_dmaout);
		DEBUGTRDMA(pr_info("dma c\n"););
	}

	port->tr_running = 1;
}

static void start_dma_in(struct sync_port *port)
{
	int i;
	char *buf;
	unsigned long flags;
	spin_lock_irqsave(&port->lock, flags);
	port->writep = port->flip;
	spin_unlock_irqrestore(&port->lock, flags);

	buf = (char *)virt_to_phys(port->in_buffer);
	for (i = 0; i < NBR_IN_DESCR; i++) {
		port->in_descr[i].buf = buf;
		port->in_descr[i].after = buf + port->inbufchunk;
		port->in_descr[i].intr = 1;
		port->in_descr[i].next =
			(dma_descr_data *)virt_to_phys(&port->in_descr[i+1]);
		port->in_descr[i].buf = buf;
		buf += port->inbufchunk;
	}
	/* Link the last descriptor to the first */
	port->in_descr[i-1].next =
		(dma_descr_data *)virt_to_phys(&port->in_descr[0]);
	port->in_descr[i-1].eol = regk_sser_yes;
	port->next_rx_desc = &port->in_descr[0];
	port->prev_rx_desc = &port->in_descr[NBR_IN_DESCR - 1];
	port->in_context.saved_data =
		(dma_descr_data *)virt_to_phys(&port->in_descr[0]);
	port->in_context.saved_data_buf = port->in_descr[0].buf;
	DMA_START_CONTEXT(port->regi_dmain, virt_to_phys(&port->in_context));
}

static irqreturn_t tr_interrupt(int irq, void *dev_id)
{
	reg_dma_r_masked_intr masked;
	reg_dma_rw_ack_intr ack_intr = { .data = regk_dma_yes };
	reg_dma_rw_stat stat;
	int i;
	int found = 0;
	int stop_sser = 0;

	for (i = 0; i < NBR_PORTS; i++) {
		struct sync_port *port = &ports[i];
		if (!port->enabled || !port->use_dma)
			continue;

		/* IRQ active for the port? */
		masked = REG_RD(dma, port->regi_dmaout, r_masked_intr);
		if (!masked.data)
			continue;

		found = 1;

		/* Check if we should stop the DMA transfer */
		stat = REG_RD(dma, port->regi_dmaout, rw_stat);
		if (stat.list_state == regk_dma_data_at_eol)
			stop_sser = 1;

		/* Clear IRQ */
		REG_WR(dma, port->regi_dmaout, rw_ack_intr, ack_intr);

		if (!stop_sser) {
			/* The DMA has completed a descriptor, EOL was not
			 * encountered, so step relevant descriptor and
			 * datapointers forward. */
			int sent;
			sent = port->catch_tr_descr->after -
				port->catch_tr_descr->buf;
			DEBUGTXINT(pr_info("%-4d - %-4d = %-4d\t"
					   "in descr %p (ac: %p)\n",
					   port->out_buf_count, sent,
					   port->out_buf_count - sent,
					   port->catch_tr_descr,
					   port->active_tr_descr););
			port->out_buf_count -= sent;
			port->catch_tr_descr =
				phys_to_virt((int) port->catch_tr_descr->next);
			port->out_rd_ptr =
				phys_to_virt((int) port->catch_tr_descr->buf);
		} else {
			reg_sser_rw_tr_cfg tr_cfg;
			int j, sent;
			/* EOL handler.
			 * Note that if an EOL was encountered during the irq
			 * locked section of sync_ser_write the DMA will be
			 * restarted and the eol flag will be cleared.
			 * The remaining descriptors will be traversed by
			 * the descriptor interrupts as usual.
			 */
			j = 0;
			while (!port->catch_tr_descr->eol) {
				sent = port->catch_tr_descr->after -
					port->catch_tr_descr->buf;
				DEBUGOUTBUF(pr_info(
					"traversing descr %p -%d (%d)\n",
					port->catch_tr_descr,
					sent,
					port->out_buf_count));
				port->out_buf_count -= sent;
				port->catch_tr_descr = phys_to_virt(
					(int)port->catch_tr_descr->next);
				j++;
				if (j >= NBR_OUT_DESCR) {
					/* TODO: Reset and recover */
					panic("sync_serial: missing eol");
				}
			}
			sent = port->catch_tr_descr->after -
				port->catch_tr_descr->buf;
			DEBUGOUTBUF(pr_info("eol at descr %p -%d (%d)\n",
				port->catch_tr_descr,
				sent,
				port->out_buf_count));

			port->out_buf_count -= sent;

			/* Update read pointer to first free byte, we
			 * may already be writing data there. */
			port->out_rd_ptr =
				phys_to_virt((int) port->catch_tr_descr->after);
			if (port->out_rd_ptr > port->out_buffer +
					OUT_BUFFER_SIZE)
				port->out_rd_ptr = port->out_buffer;

			tr_cfg = REG_RD(sser, port->regi_sser, rw_tr_cfg);
			DEBUGTXINT(pr_info(
				"tr_int DMA stop %d, set catch @ %p\n",
				port->out_buf_count,
				port->active_tr_descr));
			if (port->out_buf_count != 0)
				pr_err("sync_ser: buf not empty after eol\n");
			port->catch_tr_descr = port->active_tr_descr;
			port->tr_running = 0;
			tr_cfg.tr_en = regk_sser_no;
			REG_WR(sser, port->regi_sser, rw_tr_cfg, tr_cfg);
		}
		/* wake up the waiting process */
		wake_up_interruptible(&port->out_wait_q);
	}
	return IRQ_RETVAL(found);
} /* tr_interrupt */


static inline void handle_rx_packet(struct sync_port *port)
{
	int idx;
	reg_dma_rw_ack_intr ack_intr = { .data = regk_dma_yes };
	unsigned long flags;

	DEBUGRXINT(pr_info("!"));
	spin_lock_irqsave(&port->lock, flags);

	/* If we overrun the user experience is crap regardless if we
	 * drop new or old data. Its much easier to get it right when
	 * dropping new data so lets do that.
	 */
	if ((port->writep + port->inbufchunk <=
	     port->flip + port->in_buffer_size) &&
	    (port->in_buffer_len + port->inbufchunk < IN_BUFFER_SIZE)) {
		memcpy(port->writep,
		       phys_to_virt((unsigned)port->next_rx_desc->buf),
		       port->inbufchunk);
		port->writep += port->inbufchunk;
		if (port->writep >= port->flip + port->in_buffer_size)
			port->writep = port->flip;

		/* Timestamp the new data chunk. */
		if (port->write_ts_idx == NBR_IN_DESCR)
			port->write_ts_idx = 0;
		idx = port->write_ts_idx++;
		ktime_get_ts(&port->timestamp[idx]);
		port->in_buffer_len += port->inbufchunk;
	}
	spin_unlock_irqrestore(&port->lock, flags);

	port->next_rx_desc->eol = 1;
	port->prev_rx_desc->eol = 0;
	/* Cache bug workaround */
	flush_dma_descr(port->prev_rx_desc, 0);
	port->prev_rx_desc = port->next_rx_desc;
	port->next_rx_desc = phys_to_virt((unsigned)port->next_rx_desc->next);
	/* Cache bug workaround */
	flush_dma_descr(port->prev_rx_desc, 1);
	/* wake up the waiting process */
	wake_up_interruptible(&port->in_wait_q);
	DMA_CONTINUE(port->regi_dmain);
	REG_WR(dma, port->regi_dmain, rw_ack_intr, ack_intr);

}

static irqreturn_t rx_interrupt(int irq, void *dev_id)
{
	reg_dma_r_masked_intr masked;

	int i;
	int found = 0;

	DEBUG(pr_info("rx_interrupt\n"));

	for (i = 0; i < NBR_PORTS; i++) {
		struct sync_port *port = &ports[i];

		if (!port->enabled || !port->use_dma)
			continue;

		masked = REG_RD(dma, port->regi_dmain, r_masked_intr);

		if (!masked.data)
			continue;

		/* Descriptor interrupt */
		found = 1;
		while (REG_RD(dma, port->regi_dmain, rw_data) !=
				virt_to_phys(port->next_rx_desc))
			handle_rx_packet(port);
	}
	return IRQ_RETVAL(found);
} /* rx_interrupt */
#endif /* SYNC_SER_DMA */

#ifdef SYNC_SER_MANUAL
static irqreturn_t manual_interrupt(int irq, void *dev_id)
{
	unsigned long flags;
	int i;
	int found = 0;
	reg_sser_r_masked_intr masked;

	for (i = 0; i < NBR_PORTS; i++) {
		struct sync_port *port = &ports[i];

		if (!port->enabled || port->use_dma)
			continue;

		masked = REG_RD(sser, port->regi_sser, r_masked_intr);
		/* Data received? */
		if (masked.rdav) {
			reg_sser_rw_rec_cfg rec_cfg =
				REG_RD(sser, port->regi_sser, rw_rec_cfg);
			reg_sser_r_rec_data data = REG_RD(sser,
				port->regi_sser, r_rec_data);
			found = 1;
			/* Read data */
			spin_lock_irqsave(&port->lock, flags);
			switch (rec_cfg.sample_size) {
			case 8:
				*port->writep++ = data.data & 0xff;
				break;
			case 12:
				*port->writep = (data.data & 0x0ff0) >> 4;
				*(port->writep + 1) = data.data & 0x0f;
				port->writep += 2;
				break;
			case 16:
				*(unsigned short *)port->writep = data.data;
				port->writep += 2;
				break;
			case 24:
				*(unsigned int *)port->writep = data.data;
				port->writep += 3;
				break;
			case 32:
				*(unsigned int *)port->writep = data.data;
				port->writep += 4;
				break;
			}

			/* Wrap? */
			if (port->writep >= port->flip + port->in_buffer_size)
				port->writep = port->flip;
			if (port->writep == port->readp) {
				/* Receive buf overrun, discard oldest data */
				port->readp++;
				/* Wrap? */
				if (port->readp >= port->flip +
						port->in_buffer_size)
					port->readp = port->flip;
			}
			spin_unlock_irqrestore(&port->lock, flags);
			if (sync_data_avail(port) >= port->inbufchunk)
				/* Wake up application */
				wake_up_interruptible(&port->in_wait_q);
		}

		/* Transmitter ready? */
		if (masked.trdy) {
			found = 1;
			/* More data to send */
			if (port->out_buf_count > 0)
				send_word(port);
			else {
				/* Transmission finished */
				reg_sser_rw_intr_mask intr_mask;
				intr_mask = REG_RD(sser, port->regi_sser,
					rw_intr_mask);
				intr_mask.trdy = 0;
				REG_WR(sser, port->regi_sser,
					rw_intr_mask, intr_mask);
				/* Wake up application */
				wake_up_interruptible(&port->out_wait_q);
			}
		}
	}
	return IRQ_RETVAL(found);
}
#endif

static int __init etrax_sync_serial_init(void)
{
#if 1
	/* This code will be removed when we move to udev for all devices. */
	syncser_first = MKDEV(SYNC_SERIAL_MAJOR, 0);
	if (register_chrdev_region(syncser_first, minor_count, SYNCSER_NAME)) {
		pr_err("Failed to register major %d\n", SYNC_SERIAL_MAJOR);
		return -1;
	}
#else
	/* Allocate dynamic major number. */
	if (alloc_chrdev_region(&syncser_first, 0, minor_count, SYNCSER_NAME)) {
		pr_err("Failed to allocate character device region\n");
		return -1;
	}
#endif
	syncser_cdev = cdev_alloc();
	if (!syncser_cdev) {
		pr_err("Failed to allocate cdev for syncser\n");
		unregister_chrdev_region(syncser_first, minor_count);
		return -1;
	}
	cdev_init(syncser_cdev, &syncser_fops);

	/* Create a sysfs class for syncser */
	syncser_class = class_create(THIS_MODULE, "syncser_class");
	if (IS_ERR(syncser_class)) {
		pr_err("Failed to create a sysfs class for syncser\n");
		unregister_chrdev_region(syncser_first, minor_count);
		cdev_del(syncser_cdev);
		return -1;
	}

	/* Initialize Ports */
#if defined(CONFIG_ETRAX_SYNCHRONOUS_SERIAL_PORT0)
	if (artpec_pinmux_alloc_fixed(PINMUX_SSER0)) {
		pr_warn("Unable to alloc pins for synchronous serial port 0\n");
		unregister_chrdev_region(syncser_first, minor_count);
		return -EIO;
	}
	initialize_port(0);
	ports[0].enabled = 1;
	/* Register with sysfs so udev can pick it up. */
	device_create(syncser_class, NULL, syncser_first, NULL,
		      "%s%d", SYNCSER_NAME, 0);
#endif

#if defined(CONFIG_ETRAXFS) && defined(CONFIG_ETRAX_SYNCHRONOUS_SERIAL_PORT1)
	if (artpec_pinmux_alloc_fixed(PINMUX_SSER1)) {
		pr_warn("Unable to alloc pins for synchronous serial port 1\n");
		unregister_chrdev_region(syncser_first, minor_count);
		class_destroy(syncser_class);
		return -EIO;
	}
	initialize_port(1);
	ports[1].enabled = 1;
	/* Register with sysfs so udev can pick it up. */
	device_create(syncser_class, NULL, syncser_first, NULL,
		      "%s%d", SYNCSER_NAME, 0);
#endif

	/* Add it to system */
	if (cdev_add(syncser_cdev, syncser_first, minor_count) < 0) {
		pr_err("Failed to add syncser as char device\n");
		device_destroy(syncser_class, syncser_first);
		class_destroy(syncser_class);
		cdev_del(syncser_cdev);
		unregister_chrdev_region(syncser_first, minor_count);
		return -1;
	}


	pr_info("ARTPEC synchronous serial port (%s: %d, %d)\n",
		SYNCSER_NAME, MAJOR(syncser_first), MINOR(syncser_first));

	return 0;
}

static void __exit etrax_sync_serial_exit(void)
{
	int i;
	device_destroy(syncser_class, syncser_first);
	class_destroy(syncser_class);

	if (syncser_cdev) {
		cdev_del(syncser_cdev);
		unregister_chrdev_region(syncser_first, minor_count);
	}
	for (i = 0; i < NBR_PORTS; i++) {
		struct sync_port *port = &ports[i];
		if (port->init_irqs == dma_irq_setup) {
			/* Free dma irqs and dma channels. */
#ifdef SYNC_SER_DMA
			artpec_free_dma(port->dma_in_nbr);
			artpec_free_dma(port->dma_out_nbr);
			free_irq(port->dma_out_intr_vect, port);
			free_irq(port->dma_in_intr_vect, port);
#endif
		} else if (port->init_irqs == manual_irq_setup) {
			/* Free manual irq. */
			free_irq(port->syncser_intr_vect, port);
		}
	}

	pr_info("ARTPEC synchronous serial port unregistered\n");
}

module_init(etrax_sync_serial_init);
module_exit(etrax_sync_serial_exit);

MODULE_LICENSE("GPL");

