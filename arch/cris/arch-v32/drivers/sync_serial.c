/*
 * Simple synchronous serial port driver for ETRAX FS and Artpec-3.
 *
 * Copyright (c) 2005 Axis Communications AB
 *
 * Author: Mikael Starvik
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/major.h>
#include <linux/sched.h>
#include <linux/mutex.h>
#include <linux/interrupt.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/spinlock.h>

#include <asm/io.h>
#include <dma.h>
#include <pinmux.h>
#include <hwregs/reg_rdwr.h>
#include <hwregs/sser_defs.h>
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

#define SYNC_SERIAL_MAJOR 125

/* IN_BUFFER_SIZE should be a multiple of 6 to make sure that 24 bit */
/* words can be handled */
#define IN_BUFFER_SIZE 12288
#define IN_DESCR_SIZE 256
#define NBR_IN_DESCR (IN_BUFFER_SIZE/IN_DESCR_SIZE)

#define OUT_BUFFER_SIZE 1024*8
#define NBR_OUT_DESCR 8

#define DEFAULT_FRAME_RATE 0
#define DEFAULT_WORD_RATE 7

/* NOTE: Enabling some debug will likely cause overrun or underrun,
 * especially if manual mode is use.
 */
#define DEBUG(x)
#define DEBUGREAD(x)
#define DEBUGWRITE(x)
#define DEBUGPOLL(x)
#define DEBUGRXINT(x)
#define DEBUGTXINT(x)
#define DEBUGTRDMA(x)
#define DEBUGOUTBUF(x)

typedef struct sync_port
{
	reg_scope_instances regi_sser;
	reg_scope_instances regi_dmain;
	reg_scope_instances regi_dmaout;

	char started; /* 1 if port has been started */
	char port_nbr; /* Port 0 or 1 */
	char busy; /* 1 if port is busy */

	char enabled;  /* 1 if port is enabled */
	char use_dma;  /* 1 if port uses dma */
	char tr_running;

	char init_irqs;
	int output;
	int input;

	/* Next byte to be read by application */
	volatile unsigned char *volatile readp;
	/* Next byte to be written by etrax */
	volatile unsigned char *volatile writep;

	unsigned int in_buffer_size;
	unsigned int inbufchunk;
	unsigned char out_buffer[OUT_BUFFER_SIZE] __attribute__ ((aligned(32)));
	unsigned char in_buffer[IN_BUFFER_SIZE]__attribute__ ((aligned(32)));
	unsigned char flip[IN_BUFFER_SIZE] __attribute__ ((aligned(32)));
	struct dma_descr_data* next_rx_desc;
	struct dma_descr_data* prev_rx_desc;

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

	dma_descr_data in_descr[NBR_IN_DESCR] __attribute__ ((__aligned__(16)));
	dma_descr_context in_context __attribute__ ((__aligned__(32)));
	dma_descr_data out_descr[NBR_OUT_DESCR]
		__attribute__ ((__aligned__(16)));
	dma_descr_context out_context __attribute__ ((__aligned__(32)));
	wait_queue_head_t out_wait_q;
	wait_queue_head_t in_wait_q;

	spinlock_t lock;
} sync_port;

static DEFINE_MUTEX(sync_serial_mutex);
static int etrax_sync_serial_init(void);
static void initialize_port(int portnbr);
static inline int sync_data_avail(struct sync_port *port);

static int sync_serial_open(struct inode *, struct file*);
static int sync_serial_release(struct inode*, struct file*);
static unsigned int sync_serial_poll(struct file *filp, poll_table *wait);

static int sync_serial_ioctl(struct file *,
			     unsigned int cmd, unsigned long arg);
static ssize_t sync_serial_write(struct file * file, const char * buf,
				 size_t count, loff_t *ppos);
static ssize_t sync_serial_read(struct file *file, char *buf,
				size_t count, loff_t *ppos);

#if (defined(CONFIG_ETRAX_SYNCHRONOUS_SERIAL_PORT0) && \
     defined(CONFIG_ETRAX_SYNCHRONOUS_SERIAL0_DMA)) || \
    (defined(CONFIG_ETRAX_SYNCHRONOUS_SERIAL_PORT1) && \
     defined(CONFIG_ETRAX_SYNCHRONOUS_SERIAL1_DMA))
#define SYNC_SER_DMA
#endif

static void send_word(sync_port* port);
static void start_dma_out(struct sync_port *port, const char *data, int count);
static void start_dma_in(sync_port* port);
#ifdef SYNC_SER_DMA
static irqreturn_t tr_interrupt(int irq, void *dev_id);
static irqreturn_t rx_interrupt(int irq, void *dev_id);
#endif

#if (defined(CONFIG_ETRAX_SYNCHRONOUS_SERIAL_PORT0) && \
     !defined(CONFIG_ETRAX_SYNCHRONOUS_SERIAL0_DMA)) || \
    (defined(CONFIG_ETRAX_SYNCHRONOUS_SERIAL_PORT1) && \
     !defined(CONFIG_ETRAX_SYNCHRONOUS_SERIAL1_DMA))
#define SYNC_SER_MANUAL
#endif
#ifdef SYNC_SER_MANUAL
static irqreturn_t manual_interrupt(int irq, void *dev_id);
#endif

#ifdef CONFIG_ETRAXFS	/* ETRAX FS */
#define OUT_DMA_NBR 4
#define IN_DMA_NBR 5
#define PINMUX_SSER pinmux_sser0
#define SYNCSER_INST regi_sser0
#define SYNCSER_INTR_VECT SSER0_INTR_VECT
#define OUT_DMA_INST regi_dma4
#define IN_DMA_INST regi_dma5
#define DMA_OUT_INTR_VECT DMA4_INTR_VECT
#define DMA_IN_INTR_VECT DMA5_INTR_VECT
#define REQ_DMA_SYNCSER dma_sser0
#else			/* Artpec-3 */
#define OUT_DMA_NBR 6
#define IN_DMA_NBR 7
#define PINMUX_SSER pinmux_sser
#define SYNCSER_INST regi_sser
#define SYNCSER_INTR_VECT SSER_INTR_VECT
#define OUT_DMA_INST regi_dma6
#define IN_DMA_INST regi_dma7
#define DMA_OUT_INTR_VECT DMA6_INTR_VECT
#define DMA_IN_INTR_VECT DMA7_INTR_VECT
#define REQ_DMA_SYNCSER dma_sser
#endif

/* The ports */
static struct sync_port ports[]=
{
	{
		.regi_sser             = SYNCSER_INST,
		.regi_dmaout           = OUT_DMA_INST,
		.regi_dmain            = IN_DMA_INST,
#if defined(CONFIG_ETRAX_SYNCHRONOUS_SERIAL0_DMA)
                .use_dma               = 1,
#else
                .use_dma               = 0,
#endif
	}
#ifdef CONFIG_ETRAXFS
	,

	{
		.regi_sser             = regi_sser1,
		.regi_dmaout           = regi_dma6,
		.regi_dmain            = regi_dma7,
#if defined(CONFIG_ETRAX_SYNCHRONOUS_SERIAL1_DMA)
                .use_dma               = 1,
#else
                .use_dma               = 0,
#endif
	}
#endif
};

#define NBR_PORTS ARRAY_SIZE(ports)

static const struct file_operations sync_serial_fops = {
	.owner		= THIS_MODULE,
	.write		= sync_serial_write,
	.read		= sync_serial_read,
	.poll		= sync_serial_poll,
	.unlocked_ioctl	= sync_serial_ioctl,
	.open		= sync_serial_open,
	.release	= sync_serial_release,
	.llseek		= noop_llseek,
};

static int __init etrax_sync_serial_init(void)
{
	ports[0].enabled = 0;
#ifdef CONFIG_ETRAXFS
	ports[1].enabled = 0;
#endif
	if (register_chrdev(SYNC_SERIAL_MAJOR, "sync serial",
			&sync_serial_fops) < 0) {
		printk(KERN_WARNING
			"Unable to get major for synchronous serial port\n");
		return -EBUSY;
	}

	/* Initialize Ports */
#if defined(CONFIG_ETRAX_SYNCHRONOUS_SERIAL_PORT0)
	if (crisv32_pinmux_alloc_fixed(PINMUX_SSER)) {
		printk(KERN_WARNING
			"Unable to alloc pins for synchronous serial port 0\n");
		return -EIO;
	}
	ports[0].enabled = 1;
	initialize_port(0);
#endif

#if defined(CONFIG_ETRAX_SYNCHRONOUS_SERIAL_PORT1)
	if (crisv32_pinmux_alloc_fixed(pinmux_sser1)) {
		printk(KERN_WARNING
			"Unable to alloc pins for synchronous serial port 0\n");
		return -EIO;
	}
	ports[1].enabled = 1;
	initialize_port(1);
#endif

#ifdef CONFIG_ETRAXFS
	printk(KERN_INFO "ETRAX FS synchronous serial port driver\n");
#else
	printk(KERN_INFO "Artpec-3 synchronous serial port driver\n");
#endif
	return 0;
}

static void __init initialize_port(int portnbr)
{
	int __attribute__((unused)) i;
	struct sync_port *port = &ports[portnbr];
	reg_sser_rw_cfg cfg = {0};
	reg_sser_rw_frm_cfg frm_cfg = {0};
	reg_sser_rw_tr_cfg tr_cfg = {0};
	reg_sser_rw_rec_cfg rec_cfg = {0};

	DEBUG(printk(KERN_DEBUG "Init sync serial port %d\n", portnbr));

	port->port_nbr = portnbr;
	port->init_irqs = 1;

	port->out_rd_ptr = port->out_buffer;
	port->out_buf_count = 0;

	port->output = 1;
	port->input = 0;

	port->readp = port->flip;
	port->writep = port->flip;
	port->in_buffer_size = IN_BUFFER_SIZE;
	port->inbufchunk = IN_DESCR_SIZE;
	port->next_rx_desc = &port->in_descr[0];
	port->prev_rx_desc = &port->in_descr[NBR_IN_DESCR-1];
	port->prev_rx_desc->eol = 1;

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
	/* Setup the descriptor ring for dma out/transmit. */
	for (i = 0; i < NBR_OUT_DESCR; i++) {
		port->out_descr[i].wait = 0;
		port->out_descr[i].intr = 1;
		port->out_descr[i].eol = 0;
		port->out_descr[i].out_eop = 0;
		port->out_descr[i].next =
			(dma_descr_data *)virt_to_phys(&port->out_descr[i+1]);
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
	int avail;
	unsigned char *start;
	unsigned char *end;

	start = (unsigned char*)port->readp; /* cast away volatile */
	end = (unsigned char*)port->writep;  /* cast away volatile */
	/* 0123456789  0123456789
	 *  -----      -    -----
	 *  ^rp  ^wp    ^wp ^rp
	 */

	if (end >= start)
		avail = end - start;
	else
		avail = port->in_buffer_size - (start - end);
	return avail;
}

static inline int sync_data_avail_to_end(struct sync_port *port)
{
	int avail;
	unsigned char *start;
	unsigned char *end;

	start = (unsigned char*)port->readp; /* cast away volatile */
	end = (unsigned char*)port->writep;  /* cast away volatile */
	/* 0123456789  0123456789
	 *  -----           -----
	 *  ^rp  ^wp    ^wp ^rp
	 */

	if (end >= start)
		avail = end - start;
	else
		avail = port->flip + port->in_buffer_size - start;
	return avail;
}

static int sync_serial_open(struct inode *inode, struct file *file)
{
	int dev = iminor(inode);
	int ret = -EBUSY;
	sync_port *port;
	reg_dma_rw_cfg cfg = {.en = regk_dma_yes};
	reg_dma_rw_intr_mask intr_mask = {.data = regk_dma_yes};

	mutex_lock(&sync_serial_mutex);
	DEBUG(printk(KERN_DEBUG "Open sync serial port %d\n", dev));

	if (dev < 0 || dev >= NBR_PORTS || !ports[dev].enabled)
	{
		DEBUG(printk(KERN_DEBUG "Invalid minor %d\n", dev));
		ret = -ENODEV;
		goto out;
	}
	port = &ports[dev];
	/* Allow open this device twice (assuming one reader and one writer) */
	if (port->busy == 2)
	{
		DEBUG(printk(KERN_DEBUG "Device is busy.. \n"));
		goto out;
	}


	if (port->init_irqs) {
		if (port->use_dma) {
			if (port == &ports[0]) {
#ifdef SYNC_SER_DMA
				if (request_irq(DMA_OUT_INTR_VECT,
						tr_interrupt,
						0,
						"synchronous serial 0 dma tr",
						&ports[0])) {
					printk(KERN_CRIT "Can't allocate sync serial port 0 IRQ");
					goto out;
				} else if (request_irq(DMA_IN_INTR_VECT,
						rx_interrupt,
						0,
						"synchronous serial 1 dma rx",
						&ports[0])) {
					free_irq(DMA_OUT_INTR_VECT, &port[0]);
					printk(KERN_CRIT "Can't allocate sync serial port 0 IRQ");
					goto out;
				} else if (crisv32_request_dma(OUT_DMA_NBR,
						"synchronous serial 0 dma tr",
						DMA_VERBOSE_ON_ERROR,
						0,
						REQ_DMA_SYNCSER)) {
					free_irq(DMA_OUT_INTR_VECT, &port[0]);
					free_irq(DMA_IN_INTR_VECT, &port[0]);
					printk(KERN_CRIT "Can't allocate sync serial port 0 TX DMA channel");
					goto out;
				} else if (crisv32_request_dma(IN_DMA_NBR,
						"synchronous serial 0 dma rec",
						DMA_VERBOSE_ON_ERROR,
						0,
						REQ_DMA_SYNCSER)) {
					crisv32_free_dma(OUT_DMA_NBR);
					free_irq(DMA_OUT_INTR_VECT, &port[0]);
					free_irq(DMA_IN_INTR_VECT, &port[0]);
					printk(KERN_CRIT "Can't allocate sync serial port 1 RX DMA channel");
					goto out;
				}
#endif
			}
#ifdef CONFIG_ETRAXFS
			else if (port == &ports[1]) {
#ifdef SYNC_SER_DMA
				if (request_irq(DMA6_INTR_VECT,
						tr_interrupt,
						0,
						"synchronous serial 1 dma tr",
						&ports[1])) {
					printk(KERN_CRIT "Can't allocate sync serial port 1 IRQ");
					goto out;
				} else if (request_irq(DMA7_INTR_VECT,
						       rx_interrupt,
						       0,
						       "synchronous serial 1 dma rx",
						       &ports[1])) {
					free_irq(DMA6_INTR_VECT, &ports[1]);
					printk(KERN_CRIT "Can't allocate sync serial port 3 IRQ");
					goto out;
				} else if (crisv32_request_dma(
						SYNC_SER1_TX_DMA_NBR,
						"synchronous serial 1 dma tr",
						DMA_VERBOSE_ON_ERROR,
						0,
						dma_sser1)) {
					free_irq(DMA6_INTR_VECT, &ports[1]);
					free_irq(DMA7_INTR_VECT, &ports[1]);
					printk(KERN_CRIT "Can't allocate sync serial port 3 TX DMA channel");
					goto out;
				} else if (crisv32_request_dma(
						SYNC_SER1_RX_DMA_NBR,
						"synchronous serial 3 dma rec",
						DMA_VERBOSE_ON_ERROR,
						0,
						dma_sser1)) {
					crisv32_free_dma(SYNC_SER1_TX_DMA_NBR);
					free_irq(DMA6_INTR_VECT, &ports[1]);
					free_irq(DMA7_INTR_VECT, &ports[1]);
					printk(KERN_CRIT "Can't allocate sync serial port 3 RX DMA channel");
					goto out;
				}
#endif
			}
#endif
                        /* Enable DMAs */
			REG_WR(dma, port->regi_dmain, rw_cfg, cfg);
			REG_WR(dma, port->regi_dmaout, rw_cfg, cfg);
			/* Enable DMA IRQs */
			REG_WR(dma, port->regi_dmain, rw_intr_mask, intr_mask);
			REG_WR(dma, port->regi_dmaout, rw_intr_mask, intr_mask);
			/* Set up wordsize = 1 for DMAs. */
			DMA_WR_CMD (port->regi_dmain, regk_dma_set_w_size1);
			DMA_WR_CMD (port->regi_dmaout, regk_dma_set_w_size1);

			start_dma_in(port);
			port->init_irqs = 0;
		} else { /* !port->use_dma */
#ifdef SYNC_SER_MANUAL
			if (port == &ports[0]) {
				if (request_irq(SYNCSER_INTR_VECT,
						manual_interrupt,
						0,
						"synchronous serial manual irq",
						&ports[0])) {
					printk("Can't allocate sync serial manual irq");
					goto out;
				}
			}
#ifdef CONFIG_ETRAXFS
			else if (port == &ports[1]) {
				if (request_irq(SSER1_INTR_VECT,
						manual_interrupt,
						0,
						"synchronous serial manual irq",
						&ports[1])) {
					printk(KERN_CRIT "Can't allocate sync serial manual irq");
					goto out;
				}
			}
#endif
			port->init_irqs = 0;
#else
			panic("sync_serial: Manual mode not supported.\n");
#endif /* SYNC_SER_MANUAL */
		}

	} /* port->init_irqs */

	port->busy++;
	ret = 0;
out:
	mutex_unlock(&sync_serial_mutex);
	return ret;
}

static int sync_serial_release(struct inode *inode, struct file *file)
{
	int dev = iminor(inode);
	sync_port *port;

	if (dev < 0 || dev >= NBR_PORTS || !ports[dev].enabled)
	{
		DEBUG(printk("Invalid minor %d\n", dev));
		return -ENODEV;
	}
	port = &ports[dev];
	if (port->busy)
		port->busy--;
	if (!port->busy)
          /* XXX */ ;
	return 0;
}

static unsigned int sync_serial_poll(struct file *file, poll_table *wait)
{
	int dev = iminor(file->f_path.dentry->d_inode);
	unsigned int mask = 0;
	sync_port *port;
	DEBUGPOLL( static unsigned int prev_mask = 0; );

	port = &ports[dev];

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

	DEBUGPOLL(if (mask != prev_mask)
	      printk("sync_serial_poll: mask 0x%08X %s %s\n", mask,
		     mask&POLLOUT?"POLLOUT":"", mask&POLLIN?"POLLIN":"");
	      prev_mask = mask;
	      );
	return mask;
}

static int sync_serial_ioctl(struct file *file,
		  unsigned int cmd, unsigned long arg)
{
	int return_val = 0;
	int dma_w_size = regk_dma_set_w_size1;
	int dev = iminor(file->f_path.dentry->d_inode);
	sync_port *port;
	reg_sser_rw_tr_cfg tr_cfg;
	reg_sser_rw_rec_cfg rec_cfg;
	reg_sser_rw_frm_cfg frm_cfg;
	reg_sser_rw_cfg gen_cfg;
	reg_sser_rw_intr_mask intr_mask;

	if (dev < 0 || dev >= NBR_PORTS || !ports[dev].enabled)
	{
		DEBUG(printk("Invalid minor %d\n", dev));
		return -1;
	}
        port = &ports[dev];
	spin_lock_irq(&port->lock);

	tr_cfg = REG_RD(sser, port->regi_sser, rw_tr_cfg);
	rec_cfg = REG_RD(sser, port->regi_sser, rw_rec_cfg);
	frm_cfg = REG_RD(sser, port->regi_sser, rw_frm_cfg);
	gen_cfg = REG_RD(sser, port->regi_sser, rw_cfg);
	intr_mask = REG_RD(sser, port->regi_sser, rw_intr_mask);

	switch(cmd)
	{
	case SSP_SPEED:
		if (GET_SPEED(arg) == CODEC)
		{
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
		switch(arg)
		{
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
		if (!port->use_dma || (arg == MASTER_OUTPUT || arg == SLAVE_OUTPUT))
			intr_mask.rdav = regk_sser_yes;
		break;
	case SSP_FRAME_SYNC:
		if (arg & NORMAL_SYNC) {
			frm_cfg.rec_delay = 1;
			frm_cfg.tr_delay = 1;
		}
		else if (arg & EARLY_SYNC)
			frm_cfg.rec_delay = frm_cfg.tr_delay = 0;
		else if (arg & SECOND_WORD_SYNC) {
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
		if (arg & SPI_SLAVE)
		{
			rec_cfg.clk_pol = regk_sser_neg;
			gen_cfg.clk_dir = regk_sser_in;
			port->input = 1;
			port->output = 0;
		}
		else
		{
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
static ssize_t sync_serial_write(struct file *file, const char *buf,
				 size_t count, loff_t *ppos)
{
	int dev = iminor(file->f_path.dentry->d_inode);
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
		DEBUG(printk("Invalid minor %d\n", dev));
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
	DEBUGWRITE(printk(KERN_DEBUG "W d%d c %lu a: %p c: %p\n",
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
		DEBUGWRITE(printk(KERN_DEBUG "sser%d full\n", dev));
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

	DEBUGOUTBUF(printk(KERN_DEBUG "%-4d + %-4d = %-4d     %p %p %p\n",
			   out_buf_count, trunc_count,
			   port->out_buf_count, port->out_buffer,
			   wr_ptr, buf_stop_ptr));

	/* Make sure transmitter/receiver is running */
	if (!port->started) {
		reg_sser_rw_cfg cfg = REG_RD(sser, port->regi_sser, rw_cfg);
		reg_sser_rw_rec_cfg rec_cfg = REG_RD(sser, port->regi_sser, rw_rec_cfg);
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
		start_dma_out(port, wr_ptr, trunc_count);
	} else if (!port->tr_running) {
		reg_sser_rw_intr_mask intr_mask;
		intr_mask = REG_RD(sser, port->regi_sser, rw_intr_mask);
		/* Start sender by writing data */
		send_word(port);
		/* and enable transmitter ready IRQ */
		intr_mask.trdy = 1;
		REG_WR(sser, port->regi_sser, rw_intr_mask, intr_mask);
	}
	spin_unlock_irqrestore(&port->lock, flags);

	/* Exit if non blocking */
	if (file->f_flags & O_NONBLOCK) {
		DEBUGWRITE(printk(KERN_DEBUG "w d%d c %lu  %08x\n",
				  port->port_nbr, trunc_count,
				  REG_RD_INT(dma, port->regi_dmaout, r_intr)));
		return trunc_count;
	}

	schedule();
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&port->out_wait_q, &wait);

	if (signal_pending(current))
		return -EINTR;

	DEBUGWRITE(printk(KERN_DEBUG "w d%d c %lu\n",
			  port->port_nbr, trunc_count));
	return trunc_count;
}

static ssize_t sync_serial_read(struct file * file, char * buf,
				size_t count, loff_t *ppos)
{
	int dev = iminor(file->f_path.dentry->d_inode);
	int avail;
	sync_port *port;
	unsigned char* start;
	unsigned char* end;
	unsigned long flags;

	if (dev < 0 || dev >= NBR_PORTS || !ports[dev].enabled)
	{
		DEBUG(printk("Invalid minor %d\n", dev));
		return -ENODEV;
	}
	port = &ports[dev];

	DEBUGREAD(printk("R%d c %d ri %lu wi %lu /%lu\n", dev, count, port->readp - port->flip, port->writep - port->flip, port->in_buffer_size));

	if (!port->started)
	{
		reg_sser_rw_cfg cfg = REG_RD(sser, port->regi_sser, rw_cfg);
		reg_sser_rw_tr_cfg tr_cfg = REG_RD(sser, port->regi_sser, rw_tr_cfg);
		reg_sser_rw_rec_cfg rec_cfg = REG_RD(sser, port->regi_sser, rw_rec_cfg);
		cfg.en = regk_sser_yes;
		tr_cfg.tr_en = regk_sser_yes;
		rec_cfg.rec_en = regk_sser_yes;
		REG_WR(sser, port->regi_sser, rw_cfg, cfg);
		REG_WR(sser, port->regi_sser, rw_tr_cfg, tr_cfg);
		REG_WR(sser, port->regi_sser, rw_rec_cfg, rec_cfg);
		port->started = 1;
	}

	/* Calculate number of available bytes */
	/* Save pointers to avoid that they are modified by interrupt */
	spin_lock_irqsave(&port->lock, flags);
	start = (unsigned char*)port->readp; /* cast away volatile */
	end = (unsigned char*)port->writep;  /* cast away volatile */
	spin_unlock_irqrestore(&port->lock, flags);
	while ((start == end) && !port->full) /* No data */
	{
		DEBUGREAD(printk(KERN_DEBUG "&"));
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;

		interruptible_sleep_on(&port->in_wait_q);
		if (signal_pending(current))
			return -EINTR;

		spin_lock_irqsave(&port->lock, flags);
		start = (unsigned char*)port->readp; /* cast away volatile */
		end = (unsigned char*)port->writep;  /* cast away volatile */
		spin_unlock_irqrestore(&port->lock, flags);
	}

	/* Lazy read, never return wrapped data. */
	if (port->full)
		avail = port->in_buffer_size;
	else if (end > start)
		avail = end - start;
	else
		avail = port->flip + port->in_buffer_size - start;

	count = count > avail ? avail : count;
	if (copy_to_user(buf, start, count))
		return -EFAULT;
	/* Disable interrupts while updating readp */
	spin_lock_irqsave(&port->lock, flags);
	port->readp += count;
	if (port->readp >= port->flip + port->in_buffer_size) /* Wrap? */
		port->readp = port->flip;
	port->full = 0;
	spin_unlock_irqrestore(&port->lock, flags);
	DEBUGREAD(printk("r %d\n", count));
	return count;
}

static void send_word(sync_port* port)
{
	reg_sser_rw_tr_cfg tr_cfg = REG_RD(sser, port->regi_sser, rw_tr_cfg);
	reg_sser_rw_tr_data tr_data =  {0};

	switch(tr_cfg.sample_size)
	{
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
	}
	break;
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

static void start_dma_out(struct sync_port *port,
			  const char *data, int count)
{
	port->active_tr_descr->buf = (char *) virt_to_phys((char *) data);
	port->active_tr_descr->after = port->active_tr_descr->buf + count;
	port->active_tr_descr->intr = 1;

	port->active_tr_descr->eol = 1;
	port->prev_tr_descr->eol = 0;

	DEBUGTRDMA(printk(KERN_DEBUG "Inserting eolr:%p eol@:%p\n",
		port->prev_tr_descr, port->active_tr_descr));
	port->prev_tr_descr = port->active_tr_descr;
	port->active_tr_descr = phys_to_virt((int) port->active_tr_descr->next);

	if (!port->tr_running) {
		reg_sser_rw_tr_cfg tr_cfg = REG_RD(sser, port->regi_sser,
			rw_tr_cfg);

		port->out_context.next = 0;
		port->out_context.saved_data =
			(dma_descr_data *)virt_to_phys(port->prev_tr_descr);
		port->out_context.saved_data_buf = port->prev_tr_descr->buf;

		DMA_START_CONTEXT(port->regi_dmaout,
			virt_to_phys((char *)&port->out_context));

		tr_cfg.tr_en = regk_sser_yes;
		REG_WR(sser, port->regi_sser, rw_tr_cfg, tr_cfg);
		DEBUGTRDMA(printk(KERN_DEBUG "dma s\n"););
	} else {
		DMA_CONTINUE_DATA(port->regi_dmaout);
		DEBUGTRDMA(printk(KERN_DEBUG "dma c\n"););
	}

	port->tr_running = 1;
}

static void start_dma_in(sync_port *port)
{
	int i;
	char *buf;
	port->writep = port->flip;

	if (port->writep > port->flip + port->in_buffer_size) {
		panic("Offset too large in sync serial driver\n");
		return;
	}
	buf = (char*)virt_to_phys(port->in_buffer);
	for (i = 0; i < NBR_IN_DESCR; i++) {
		port->in_descr[i].buf = buf;
		port->in_descr[i].after = buf + port->inbufchunk;
		port->in_descr[i].intr = 1;
		port->in_descr[i].next = (dma_descr_data*)virt_to_phys(&port->in_descr[i+1]);
		port->in_descr[i].buf = buf;
		buf += port->inbufchunk;
	}
	/* Link the last descriptor to the first */
	port->in_descr[i-1].next = (dma_descr_data*)virt_to_phys(&port->in_descr[0]);
	port->in_descr[i-1].eol = regk_sser_yes;
	port->next_rx_desc = &port->in_descr[0];
	port->prev_rx_desc = &port->in_descr[NBR_IN_DESCR - 1];
	port->in_context.saved_data = (dma_descr_data*)virt_to_phys(&port->in_descr[0]);
	port->in_context.saved_data_buf = port->in_descr[0].buf;
	DMA_START_CONTEXT(port->regi_dmain, virt_to_phys(&port->in_context));
}

#ifdef SYNC_SER_DMA
static irqreturn_t tr_interrupt(int irq, void *dev_id)
{
	reg_dma_r_masked_intr masked;
	reg_dma_rw_ack_intr ack_intr = {.data = regk_dma_yes};
	reg_dma_rw_stat stat;
	int i;
	int found = 0;
	int stop_sser = 0;

	for (i = 0; i < NBR_PORTS; i++) {
		sync_port *port = &ports[i];
		if (!port->enabled  || !port->use_dma)
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
			DEBUGTXINT(printk(KERN_DEBUG "%-4d - %-4d = %-4d\t"
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
			int i, sent;
			/* EOL handler.
			 * Note that if an EOL was encountered during the irq
			 * locked section of sync_ser_write the DMA will be
			 * restarted and the eol flag will be cleared.
			 * The remaining descriptors will be traversed by
			 * the descriptor interrupts as usual.
			 */
			i = 0;
			while (!port->catch_tr_descr->eol) {
				sent = port->catch_tr_descr->after -
					port->catch_tr_descr->buf;
				DEBUGOUTBUF(printk(KERN_DEBUG
					"traversing descr %p -%d (%d)\n",
					port->catch_tr_descr,
					sent,
					port->out_buf_count));
				port->out_buf_count -= sent;
				port->catch_tr_descr = phys_to_virt(
					(int)port->catch_tr_descr->next);
				i++;
				if (i >= NBR_OUT_DESCR) {
					/* TODO: Reset and recover */
					panic("sync_serial: missing eol");
				}
			}
			sent = port->catch_tr_descr->after -
				port->catch_tr_descr->buf;
			DEBUGOUTBUF(printk(KERN_DEBUG
				"eol at descr %p -%d (%d)\n",
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

			reg_sser_rw_tr_cfg tr_cfg =
				REG_RD(sser, port->regi_sser, rw_tr_cfg);
			DEBUGTXINT(printk(KERN_DEBUG
				"tr_int DMA stop %d, set catch @ %p\n",
				port->out_buf_count,
				port->active_tr_descr));
			if (port->out_buf_count != 0)
				printk(KERN_CRIT "sync_ser: buffer not "
					"empty after eol.\n");
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

static irqreturn_t rx_interrupt(int irq, void *dev_id)
{
	reg_dma_r_masked_intr masked;
	reg_dma_rw_ack_intr ack_intr = {.data = regk_dma_yes};

	int i;
	int found = 0;

	for (i = 0; i < NBR_PORTS; i++)
	{
		sync_port *port = &ports[i];

		if (!port->enabled || !port->use_dma )
			continue;

		masked = REG_RD(dma, port->regi_dmain, r_masked_intr);

		if (masked.data) /* Descriptor interrupt */
		{
			found = 1;
			while (REG_RD(dma, port->regi_dmain, rw_data) !=
			       virt_to_phys(port->next_rx_desc)) {
				DEBUGRXINT(printk(KERN_DEBUG "!"));
				if (port->writep + port->inbufchunk > port->flip + port->in_buffer_size) {
					int first_size = port->flip + port->in_buffer_size - port->writep;
					memcpy((char*)port->writep, phys_to_virt((unsigned)port->next_rx_desc->buf), first_size);
					memcpy(port->flip, phys_to_virt((unsigned)port->next_rx_desc->buf+first_size), port->inbufchunk - first_size);
					port->writep = port->flip + port->inbufchunk - first_size;
				} else {
					memcpy((char*)port->writep,
					       phys_to_virt((unsigned)port->next_rx_desc->buf),
					       port->inbufchunk);
					port->writep += port->inbufchunk;
					if (port->writep >= port->flip + port->in_buffer_size)
						port->writep = port->flip;
				}
                                if (port->writep == port->readp)
                                {
				  port->full = 1;
                                }

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
		}
	}
	return IRQ_RETVAL(found);
} /* rx_interrupt */
#endif /* SYNC_SER_DMA */

#ifdef SYNC_SER_MANUAL
static irqreturn_t manual_interrupt(int irq, void *dev_id)
{
	int i;
	int found = 0;
	reg_sser_r_masked_intr masked;

	for (i = 0; i < NBR_PORTS; i++)
	{
		sync_port *port = &ports[i];

		if (!port->enabled || port->use_dma)
		{
			continue;
		}

		masked = REG_RD(sser, port->regi_sser, r_masked_intr);
		if (masked.rdav)	/* Data received? */
		{
			reg_sser_rw_rec_cfg rec_cfg = REG_RD(sser, port->regi_sser, rw_rec_cfg);
			reg_sser_r_rec_data data = REG_RD(sser, port->regi_sser, r_rec_data);
			found = 1;
			/* Read data */
			switch(rec_cfg.sample_size)
			{
			case 8:
				*port->writep++ = data.data & 0xff;
				break;
			case 12:
				*port->writep = (data.data & 0x0ff0) >> 4;
				*(port->writep + 1) = data.data & 0x0f;
				port->writep+=2;
				break;
			case 16:
				*(unsigned short*)port->writep = data.data;
				port->writep+=2;
				break;
			case 24:
				*(unsigned int*)port->writep = data.data;
				port->writep+=3;
				break;
			case 32:
				*(unsigned int*)port->writep = data.data;
				port->writep+=4;
				break;
			}

			if (port->writep >= port->flip + port->in_buffer_size) /* Wrap? */
				port->writep = port->flip;
			if (port->writep == port->readp) {
				/* receive buffer overrun, discard oldest data
				 */
				port->readp++;
				if (port->readp >= port->flip + port->in_buffer_size) /* Wrap? */
					port->readp = port->flip;
			}
			if (sync_data_avail(port) >= port->inbufchunk)
				wake_up_interruptible(&port->in_wait_q); /* Wake up application */
		}

		if (masked.trdy) /* Transmitter ready? */
		{
			found = 1;
			if (port->out_buf_count > 0) /* More data to send */
				send_word(port);
			else /* transmission finished */
			{
				reg_sser_rw_intr_mask intr_mask;
				intr_mask = REG_RD(sser, port->regi_sser, rw_intr_mask);
				intr_mask.trdy = 0;
				REG_WR(sser, port->regi_sser, rw_intr_mask, intr_mask);
				wake_up_interruptible(&port->out_wait_q); /* Wake up application */
			}
		}
	}
	return IRQ_RETVAL(found);
}
#endif

module_init(etrax_sync_serial_init);
