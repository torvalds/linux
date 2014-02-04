/*
 * Simple synchronous serial port driver for ETRAX 100LX.
 *
 * Synchronous serial ports are used for continuous streamed data like audio.
 * The default setting for this driver is compatible with the STA 013 MP3
 * decoder. The driver can easily be tuned to fit other audio encoder/decoders
 * and SPI
 *
 * Copyright (c) 2001-2008 Axis Communications AB
 *
 * Author: Mikael Starvik, Johan Adolfsson
 *
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/major.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/mutex.h>
#include <linux/timer.h>
#include <linux/wait.h>
#include <asm/irq.h>
#include <asm/dma.h>
#include <asm/io.h>
#include <arch/svinto.h>
#include <asm/uaccess.h>
#include <asm/sync_serial.h>
#include <arch/io_interface_mux.h>

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
#define NUM_IN_DESCR (IN_BUFFER_SIZE/IN_DESCR_SIZE)
#define OUT_BUFFER_SIZE 4096

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

/* Define some macros to access ETRAX 100 registers */
#define SETF(var, reg, field, val) \
	do { \
		var = (var & ~IO_MASK_(reg##_, field##_)) | \
			IO_FIELD_(reg##_, field##_, val); \
	} while (0)

#define SETS(var, reg, field, val) \
	do { \
		var = (var & ~IO_MASK_(reg##_, field##_)) | \
			IO_STATE_(reg##_, field##_, _##val); \
	} while (0)

struct sync_port {
	/* Etrax registers and bits*/
	const volatile unsigned *const status;
	volatile unsigned *const ctrl_data;
	volatile unsigned *const output_dma_first;
	volatile unsigned char *const output_dma_cmd;
	volatile unsigned char *const output_dma_clr_irq;
	volatile unsigned *const input_dma_first;
	volatile unsigned char *const input_dma_cmd;
	volatile unsigned *const input_dma_descr;
	/* 8*4 */
	volatile unsigned char *const input_dma_clr_irq;
	volatile unsigned *const data_out;
	const volatile unsigned *const data_in;
	char data_avail_bit; /* In R_IRQ_MASK1_RD/SET/CLR */
	char transmitter_ready_bit; /* In R_IRQ_MASK1_RD/SET/CLR */
	char input_dma_descr_bit; /* In R_IRQ_MASK2_RD */

	char output_dma_bit; /* In R_IRQ_MASK2_RD */
	/* End of fields initialised in array */
	char started; /* 1 if port has been started */
	char port_nbr; /* Port 0 or 1 */
	char busy; /* 1 if port is busy */

	char enabled;  /* 1 if port is enabled */
	char use_dma;  /* 1 if port uses dma */
	char tr_running;

	char init_irqs;

	/* Register shadow */
	unsigned int ctrl_data_shadow;
	/* Remaining bytes for current transfer */
	volatile unsigned int out_count;
	/* Current position in out_buffer */
	unsigned char *outp;
	/* 16*4 */
	/* Next byte to be read by application */
	volatile unsigned char *volatile readp;
	/* Next byte to be written by etrax */
	volatile unsigned char *volatile writep;

	unsigned int in_buffer_size;
	unsigned int inbufchunk;
	struct etrax_dma_descr out_descr __attribute__ ((aligned(32)));
	struct etrax_dma_descr in_descr[NUM_IN_DESCR] __attribute__ ((aligned(32)));
	unsigned char out_buffer[OUT_BUFFER_SIZE] __attribute__ ((aligned(32)));
	unsigned char in_buffer[IN_BUFFER_SIZE]__attribute__ ((aligned(32)));
	unsigned char flip[IN_BUFFER_SIZE] __attribute__ ((aligned(32)));
	struct etrax_dma_descr *next_rx_desc;
	struct etrax_dma_descr *prev_rx_desc;
	int full;

	wait_queue_head_t out_wait_q;
	wait_queue_head_t in_wait_q;
};


static DEFINE_MUTEX(sync_serial_mutex);
static int etrax_sync_serial_init(void);
static void initialize_port(int portnbr);
static inline int sync_data_avail(struct sync_port *port);

static int sync_serial_open(struct inode *inode, struct file *file);
static int sync_serial_release(struct inode *inode, struct file *file);
static unsigned int sync_serial_poll(struct file *filp, poll_table *wait);

static long sync_serial_ioctl(struct file *file,
	unsigned int cmd, unsigned long arg);
static ssize_t sync_serial_write(struct file *file, const char *buf,
	size_t count, loff_t *ppos);
static ssize_t sync_serial_read(struct file *file, char *buf,
	size_t count, loff_t *ppos);

#if ((defined(CONFIG_ETRAX_SYNCHRONOUS_SERIAL_PORT0) && \
     defined(CONFIG_ETRAX_SYNCHRONOUS_SERIAL0_DMA)) || \
    (defined(CONFIG_ETRAX_SYNCHRONOUS_SERIAL_PORT1) && \
     defined(CONFIG_ETRAX_SYNCHRONOUS_SERIAL1_DMA)))
#define SYNC_SER_DMA
#endif

static void send_word(struct sync_port *port);
static void start_dma(struct sync_port *port, const char *data, int count);
static void start_dma_in(struct sync_port *port);
#ifdef SYNC_SER_DMA
static irqreturn_t tr_interrupt(int irq, void *dev_id);
static irqreturn_t rx_interrupt(int irq, void *dev_id);
#endif
#if ((defined(CONFIG_ETRAX_SYNCHRONOUS_SERIAL_PORT0) && \
     !defined(CONFIG_ETRAX_SYNCHRONOUS_SERIAL0_DMA)) || \
    (defined(CONFIG_ETRAX_SYNCHRONOUS_SERIAL_PORT1) && \
     !defined(CONFIG_ETRAX_SYNCHRONOUS_SERIAL1_DMA)))
#define SYNC_SER_MANUAL
#endif
#ifdef SYNC_SER_MANUAL
static irqreturn_t manual_interrupt(int irq, void *dev_id);
#endif

/* The ports */
static struct sync_port ports[] = {
	{
		.status                = R_SYNC_SERIAL1_STATUS,
		.ctrl_data             = R_SYNC_SERIAL1_CTRL,
		.output_dma_first      = R_DMA_CH8_FIRST,
		.output_dma_cmd        = R_DMA_CH8_CMD,
		.output_dma_clr_irq    = R_DMA_CH8_CLR_INTR,
		.input_dma_first       = R_DMA_CH9_FIRST,
		.input_dma_cmd         = R_DMA_CH9_CMD,
		.input_dma_descr       = R_DMA_CH9_DESCR,
		.input_dma_clr_irq     = R_DMA_CH9_CLR_INTR,
		.data_out              = R_SYNC_SERIAL1_TR_DATA,
		.data_in               = R_SYNC_SERIAL1_REC_DATA,
		.data_avail_bit        = IO_BITNR(R_IRQ_MASK1_RD, ser1_data),
		.transmitter_ready_bit = IO_BITNR(R_IRQ_MASK1_RD, ser1_ready),
		.input_dma_descr_bit   = IO_BITNR(R_IRQ_MASK2_RD, dma9_descr),
		.output_dma_bit        = IO_BITNR(R_IRQ_MASK2_RD, dma8_eop),
		.init_irqs             = 1,
#if defined(CONFIG_ETRAX_SYNCHRONOUS_SERIAL0_DMA)
		.use_dma               = 1,
#else
		.use_dma               = 0,
#endif
	},
	{
		.status                = R_SYNC_SERIAL3_STATUS,
		.ctrl_data             = R_SYNC_SERIAL3_CTRL,
		.output_dma_first      = R_DMA_CH4_FIRST,
		.output_dma_cmd        = R_DMA_CH4_CMD,
		.output_dma_clr_irq    = R_DMA_CH4_CLR_INTR,
		.input_dma_first       = R_DMA_CH5_FIRST,
		.input_dma_cmd         = R_DMA_CH5_CMD,
		.input_dma_descr       = R_DMA_CH5_DESCR,
		.input_dma_clr_irq     = R_DMA_CH5_CLR_INTR,
		.data_out              = R_SYNC_SERIAL3_TR_DATA,
		.data_in               = R_SYNC_SERIAL3_REC_DATA,
		.data_avail_bit        = IO_BITNR(R_IRQ_MASK1_RD, ser3_data),
		.transmitter_ready_bit = IO_BITNR(R_IRQ_MASK1_RD, ser3_ready),
		.input_dma_descr_bit   = IO_BITNR(R_IRQ_MASK2_RD, dma5_descr),
		.output_dma_bit        = IO_BITNR(R_IRQ_MASK2_RD, dma4_eop),
		.init_irqs             = 1,
#if defined(CONFIG_ETRAX_SYNCHRONOUS_SERIAL1_DMA)
		.use_dma               = 1,
#else
		.use_dma               = 0,
#endif
	}
};

/* Register shadows */
static unsigned sync_serial_prescale_shadow;

#define NUMBER_OF_PORTS 2

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
	ports[1].enabled = 0;

#if defined(CONFIG_ETRAX_SYNCHRONOUS_SERIAL_PORT0)
	if (cris_request_io_interface(if_sync_serial_1, "sync_ser1")) {
		printk(KERN_CRIT "ETRAX100LX sync_serial: "
			"Could not allocate IO group for port %d\n", 0);
		return -EBUSY;
	}
#endif
#if defined(CONFIG_ETRAX_SYNCHRONOUS_SERIAL_PORT1)
	if (cris_request_io_interface(if_sync_serial_3, "sync_ser3")) {
#if defined(CONFIG_ETRAX_SYNCHRONOUS_SERIAL_PORT0)
		cris_free_io_interface(if_sync_serial_1);
#endif
		printk(KERN_CRIT "ETRAX100LX sync_serial: "
			"Could not allocate IO group for port %d\n", 1);
		return -EBUSY;
	}
#endif

	if (register_chrdev(SYNC_SERIAL_MAJOR, "sync serial",
			&sync_serial_fops) < 0) {
#if defined(CONFIG_ETRAX_SYNCHRONOUS_SERIAL_PORT1)
		cris_free_io_interface(if_sync_serial_3);
#endif
#if defined(CONFIG_ETRAX_SYNCHRONOUS_SERIAL_PORT0)
		cris_free_io_interface(if_sync_serial_1);
#endif
		printk("unable to get major for synchronous serial port\n");
		return -EBUSY;
	}

	/* Deselect synchronous serial ports while configuring. */
	SETS(gen_config_ii_shadow, R_GEN_CONFIG_II, sermode1, async);
	SETS(gen_config_ii_shadow, R_GEN_CONFIG_II, sermode3, async);
	*R_GEN_CONFIG_II = gen_config_ii_shadow;

	/* Initialize Ports */
#if defined(CONFIG_ETRAX_SYNCHRONOUS_SERIAL_PORT0)
	ports[0].enabled = 1;
	SETS(port_pb_i2c_shadow, R_PORT_PB_I2C, syncser1, ss1extra);
	SETS(gen_config_ii_shadow, R_GEN_CONFIG_II, sermode1, sync);
#if defined(CONFIG_ETRAX_SYNCHRONOUS_SERIAL0_DMA)
	ports[0].use_dma = 1;
#else
	ports[0].use_dma = 0;
#endif
	initialize_port(0);
#endif

#if defined(CONFIG_ETRAX_SYNCHRONOUS_SERIAL_PORT1)
	ports[1].enabled = 1;
	SETS(port_pb_i2c_shadow, R_PORT_PB_I2C, syncser3, ss3extra);
	SETS(gen_config_ii_shadow, R_GEN_CONFIG_II, sermode3, sync);
#if defined(CONFIG_ETRAX_SYNCHRONOUS_SERIAL1_DMA)
	ports[1].use_dma = 1;
#else
	ports[1].use_dma = 0;
#endif
	initialize_port(1);
#endif

	*R_PORT_PB_I2C = port_pb_i2c_shadow; /* Use PB4/PB7 */

	/* Set up timing */
	*R_SYNC_SERIAL_PRESCALE = sync_serial_prescale_shadow = (
		IO_STATE(R_SYNC_SERIAL_PRESCALE, clk_sel_u1, codec) |
		IO_STATE(R_SYNC_SERIAL_PRESCALE, word_stb_sel_u1, external) |
		IO_STATE(R_SYNC_SERIAL_PRESCALE, clk_sel_u3, codec) |
		IO_STATE(R_SYNC_SERIAL_PRESCALE, word_stb_sel_u3, external) |
		IO_STATE(R_SYNC_SERIAL_PRESCALE, prescaler, div4) |
		IO_FIELD(R_SYNC_SERIAL_PRESCALE, frame_rate,
			DEFAULT_FRAME_RATE) |
		IO_FIELD(R_SYNC_SERIAL_PRESCALE, word_rate, DEFAULT_WORD_RATE) |
		IO_STATE(R_SYNC_SERIAL_PRESCALE, warp_mode, normal));

	/* Select synchronous ports */
	*R_GEN_CONFIG_II = gen_config_ii_shadow;

	printk(KERN_INFO "ETRAX 100LX synchronous serial port driver\n");
	return 0;
}

static void __init initialize_port(int portnbr)
{
	struct sync_port *port = &ports[portnbr];

	DEBUG(printk(KERN_DEBUG "Init sync serial port %d\n", portnbr));

	port->started = 0;
	port->port_nbr = portnbr;
	port->busy = 0;
	port->tr_running = 0;

	port->out_count = 0;
	port->outp = port->out_buffer;

	port->readp = port->flip;
	port->writep = port->flip;
	port->in_buffer_size = IN_BUFFER_SIZE;
	port->inbufchunk = IN_DESCR_SIZE;
	port->next_rx_desc = &port->in_descr[0];
	port->prev_rx_desc = &port->in_descr[NUM_IN_DESCR-1];
	port->prev_rx_desc->ctrl = d_eol;

	init_waitqueue_head(&port->out_wait_q);
	init_waitqueue_head(&port->in_wait_q);

	port->ctrl_data_shadow =
		IO_STATE(R_SYNC_SERIAL1_CTRL, tr_baud, c115k2Hz)   |
		IO_STATE(R_SYNC_SERIAL1_CTRL, mode, master_output) |
		IO_STATE(R_SYNC_SERIAL1_CTRL, error, ignore)       |
		IO_STATE(R_SYNC_SERIAL1_CTRL, rec_enable, disable) |
		IO_STATE(R_SYNC_SERIAL1_CTRL, f_synctype, normal)  |
		IO_STATE(R_SYNC_SERIAL1_CTRL, f_syncsize, word)    |
		IO_STATE(R_SYNC_SERIAL1_CTRL, f_sync, on)	     |
		IO_STATE(R_SYNC_SERIAL1_CTRL, clk_mode, normal)    |
		IO_STATE(R_SYNC_SERIAL1_CTRL, clk_halt, stopped)   |
		IO_STATE(R_SYNC_SERIAL1_CTRL, bitorder, msb)	     |
		IO_STATE(R_SYNC_SERIAL1_CTRL, tr_enable, disable)  |
		IO_STATE(R_SYNC_SERIAL1_CTRL, wordsize, size8bit)  |
		IO_STATE(R_SYNC_SERIAL1_CTRL, buf_empty, lmt_8)    |
		IO_STATE(R_SYNC_SERIAL1_CTRL, buf_full, lmt_8)     |
		IO_STATE(R_SYNC_SERIAL1_CTRL, flow_ctrl, enabled)  |
		IO_STATE(R_SYNC_SERIAL1_CTRL, clk_polarity, neg)   |
		IO_STATE(R_SYNC_SERIAL1_CTRL, frame_polarity, normal)|
		IO_STATE(R_SYNC_SERIAL1_CTRL, status_polarity, inverted)|
		IO_STATE(R_SYNC_SERIAL1_CTRL, clk_driver, normal)   |
		IO_STATE(R_SYNC_SERIAL1_CTRL, frame_driver, normal) |
		IO_STATE(R_SYNC_SERIAL1_CTRL, status_driver, normal)|
		IO_STATE(R_SYNC_SERIAL1_CTRL, def_out0, high);

	if (port->use_dma)
		port->ctrl_data_shadow |= IO_STATE(R_SYNC_SERIAL1_CTRL,
			dma_enable, on);
	else
		port->ctrl_data_shadow |= IO_STATE(R_SYNC_SERIAL1_CTRL,
			dma_enable, off);

	*port->ctrl_data = port->ctrl_data_shadow;
}

static inline int sync_data_avail(struct sync_port *port)
{
	int avail;
	unsigned char *start;
	unsigned char *end;

	start = (unsigned char *)port->readp; /* cast away volatile */
	end = (unsigned char *)port->writep;  /* cast away volatile */
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

	start = (unsigned char *)port->readp; /* cast away volatile */
	end = (unsigned char *)port->writep;  /* cast away volatile */
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
	int dev = MINOR(inode->i_rdev);
	struct sync_port *port;
	int mode;
	int err = -EBUSY;

	mutex_lock(&sync_serial_mutex);
	DEBUG(printk(KERN_DEBUG "Open sync serial port %d\n", dev));

	if (dev < 0 || dev >= NUMBER_OF_PORTS || !ports[dev].enabled) {
		DEBUG(printk(KERN_DEBUG "Invalid minor %d\n", dev));
		err = -ENODEV;
		goto out;
	}
	port = &ports[dev];
	/* Allow open this device twice (assuming one reader and one writer) */
	if (port->busy == 2) {
		DEBUG(printk(KERN_DEBUG "Device is busy.. \n"));
		goto out;
	}
	if (port->init_irqs) {
		if (port->use_dma) {
			if (port == &ports[0]) {
#ifdef SYNC_SER_DMA
				if (request_irq(24, tr_interrupt, 0,
						"synchronous serial 1 dma tr",
						&ports[0])) {
					printk(KERN_CRIT "Can't alloc "
						"sync serial port 1 IRQ");
					goto out;
				} else if (request_irq(25, rx_interrupt, 0,
						"synchronous serial 1 dma rx",
						&ports[0])) {
					free_irq(24, &port[0]);
					printk(KERN_CRIT "Can't alloc "
						"sync serial port 1 IRQ");
					goto out;
				} else if (cris_request_dma(8,
						"synchronous serial 1 dma tr",
						DMA_VERBOSE_ON_ERROR,
						dma_ser1)) {
					free_irq(24, &port[0]);
					free_irq(25, &port[0]);
					printk(KERN_CRIT "Can't alloc "
						"sync serial port 1 "
						"TX DMA channel");
					goto out;
				} else if (cris_request_dma(9,
						"synchronous serial 1 dma rec",
						DMA_VERBOSE_ON_ERROR,
						dma_ser1)) {
					cris_free_dma(8, NULL);
					free_irq(24, &port[0]);
					free_irq(25, &port[0]);
					printk(KERN_CRIT "Can't alloc "
						"sync serial port 1 "
						"RX DMA channel");
					goto out;
				}
#endif
				RESET_DMA(8); WAIT_DMA(8);
				RESET_DMA(9); WAIT_DMA(9);
				*R_DMA_CH8_CLR_INTR =
					IO_STATE(R_DMA_CH8_CLR_INTR, clr_eop,
						do) |
					IO_STATE(R_DMA_CH8_CLR_INTR, clr_descr,
						do);
				*R_DMA_CH9_CLR_INTR =
					IO_STATE(R_DMA_CH9_CLR_INTR, clr_eop,
						do) |
					IO_STATE(R_DMA_CH9_CLR_INTR, clr_descr,
						do);
				*R_IRQ_MASK2_SET =
					IO_STATE(R_IRQ_MASK2_SET, dma8_eop,
						set) |
					IO_STATE(R_IRQ_MASK2_SET, dma9_descr,
						set);
			} else if (port == &ports[1]) {
#ifdef SYNC_SER_DMA
				if (request_irq(20, tr_interrupt, 0,
						"synchronous serial 3 dma tr",
						&ports[1])) {
					printk(KERN_CRIT "Can't alloc "
						"sync serial port 3 IRQ");
					goto out;
				} else if (request_irq(21, rx_interrupt, 0,
						"synchronous serial 3 dma rx",
						&ports[1])) {
					free_irq(20, &ports[1]);
					printk(KERN_CRIT "Can't alloc "
						"sync serial port 3 IRQ");
					goto out;
				} else if (cris_request_dma(4,
						"synchronous serial 3 dma tr",
						DMA_VERBOSE_ON_ERROR,
						dma_ser3)) {
					free_irq(21, &ports[1]);
					free_irq(20, &ports[1]);
					printk(KERN_CRIT "Can't alloc "
						"sync serial port 3 "
						"TX DMA channel");
					goto out;
				} else if (cris_request_dma(5,
						"synchronous serial 3 dma rec",
						DMA_VERBOSE_ON_ERROR,
						dma_ser3)) {
					cris_free_dma(4, NULL);
					free_irq(21, &ports[1]);
					free_irq(20, &ports[1]);
					printk(KERN_CRIT "Can't alloc "
						"sync serial port 3 "
						"RX DMA channel");
					goto out;
				}
#endif
				RESET_DMA(4); WAIT_DMA(4);
				RESET_DMA(5); WAIT_DMA(5);
				*R_DMA_CH4_CLR_INTR =
					IO_STATE(R_DMA_CH4_CLR_INTR, clr_eop,
						do) |
					IO_STATE(R_DMA_CH4_CLR_INTR, clr_descr,
						do);
				*R_DMA_CH5_CLR_INTR =
					IO_STATE(R_DMA_CH5_CLR_INTR, clr_eop,
						do) |
					IO_STATE(R_DMA_CH5_CLR_INTR, clr_descr,
						do);
				*R_IRQ_MASK2_SET =
					IO_STATE(R_IRQ_MASK2_SET, dma4_eop,
						set) |
					IO_STATE(R_IRQ_MASK2_SET, dma5_descr,
						set);
			}
			start_dma_in(port);
			port->init_irqs = 0;
		} else { /* !port->use_dma */
#ifdef SYNC_SER_MANUAL
			if (port == &ports[0]) {
				if (request_irq(8,
						manual_interrupt,
						IRQF_SHARED,
						"synchronous serial manual irq",
						&ports[0])) {
					printk(KERN_CRIT "Can't alloc "
						"sync serial manual irq");
					goto out;
				}
			} else if (port == &ports[1]) {
				if (request_irq(8,
						manual_interrupt,
						IRQF_SHARED,
						"synchronous serial manual irq",
						&ports[1])) {
					printk(KERN_CRIT "Can't alloc "
						"sync serial manual irq");
					goto out;
				}
			}
			port->init_irqs = 0;
#else
			panic("sync_serial: Manual mode not supported.\n");
#endif /* SYNC_SER_MANUAL */
		}
	} /* port->init_irqs */

	port->busy++;
	/* Start port if we use it as input */
	mode = IO_EXTRACT(R_SYNC_SERIAL1_CTRL, mode, port->ctrl_data_shadow);
	if (mode == IO_STATE_VALUE(R_SYNC_SERIAL1_CTRL, mode, master_input) ||
	    mode == IO_STATE_VALUE(R_SYNC_SERIAL1_CTRL, mode, slave_input) ||
	    mode == IO_STATE_VALUE(R_SYNC_SERIAL1_CTRL, mode, master_bidir) ||
	    mode == IO_STATE_VALUE(R_SYNC_SERIAL1_CTRL, mode, slave_bidir)) {
		SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL, clk_halt,
			running);
		SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL, tr_enable,
			enable);
		SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL, rec_enable,
			enable);
		port->started = 1;
		*port->ctrl_data = port->ctrl_data_shadow;
		if (!port->use_dma)
			*R_IRQ_MASK1_SET = 1 << port->data_avail_bit;
		DEBUG(printk(KERN_DEBUG "sser%d rec started\n", dev));
	}
	err = 0;
	
out:
	mutex_unlock(&sync_serial_mutex);
	return err;
}

static int sync_serial_release(struct inode *inode, struct file *file)
{
	int dev = MINOR(inode->i_rdev);
	struct sync_port *port;

	if (dev < 0 || dev >= NUMBER_OF_PORTS || !ports[dev].enabled) {
		DEBUG(printk(KERN_DEBUG "Invalid minor %d\n", dev));
		return -ENODEV;
	}
	port = &ports[dev];
	if (port->busy)
		port->busy--;
	if (!port->busy)
		*R_IRQ_MASK1_CLR = ((1 << port->data_avail_bit) |
				    (1 << port->transmitter_ready_bit));

	return 0;
}



static unsigned int sync_serial_poll(struct file *file, poll_table *wait)
{
	int dev = MINOR(file_inode(file)->i_rdev);
	unsigned int mask = 0;
	struct sync_port *port;
	DEBUGPOLL(static unsigned int prev_mask = 0);

	port = &ports[dev];
	poll_wait(file, &port->out_wait_q, wait);
	poll_wait(file, &port->in_wait_q, wait);
	/* Some room to write */
	if (port->out_count < OUT_BUFFER_SIZE)
		mask |=  POLLOUT | POLLWRNORM;
	/* At least an inbufchunk of data */
	if (sync_data_avail(port) >= port->inbufchunk)
		mask |= POLLIN | POLLRDNORM;

	DEBUGPOLL(if (mask != prev_mask)
		printk(KERN_DEBUG "sync_serial_poll: mask 0x%08X %s %s\n",
			mask,
			mask & POLLOUT ? "POLLOUT" : "",
			mask & POLLIN ? "POLLIN" : "");
		prev_mask = mask;
	);
	return mask;
}

static int sync_serial_ioctl_unlocked(struct file *file,
		  unsigned int cmd, unsigned long arg)
{
	int return_val = 0;
	unsigned long flags;

	int dev = MINOR(file_inode(file)->i_rdev);
	struct sync_port *port;

	if (dev < 0 || dev >= NUMBER_OF_PORTS || !ports[dev].enabled) {
		DEBUG(printk(KERN_DEBUG "Invalid minor %d\n", dev));
		return -1;
	}
	port = &ports[dev];

	local_irq_save(flags);
	/* Disable port while changing config */
	if (dev) {
		if (port->use_dma) {
			RESET_DMA(4); WAIT_DMA(4);
			port->tr_running = 0;
			port->out_count = 0;
			port->outp = port->out_buffer;
			*R_DMA_CH4_CLR_INTR =
				IO_STATE(R_DMA_CH4_CLR_INTR, clr_eop, do) |
				IO_STATE(R_DMA_CH4_CLR_INTR, clr_descr, do);
		}
		SETS(gen_config_ii_shadow, R_GEN_CONFIG_II, sermode3, async);
	} else {
		if (port->use_dma) {
			RESET_DMA(8); WAIT_DMA(8);
			port->tr_running = 0;
			port->out_count = 0;
			port->outp = port->out_buffer;
			*R_DMA_CH8_CLR_INTR =
				IO_STATE(R_DMA_CH8_CLR_INTR, clr_eop, do) |
				IO_STATE(R_DMA_CH8_CLR_INTR, clr_descr, do);
		}
		SETS(gen_config_ii_shadow, R_GEN_CONFIG_II, sermode1, async);
	}
	*R_GEN_CONFIG_II = gen_config_ii_shadow;
	local_irq_restore(flags);

	switch (cmd) {
	case SSP_SPEED:
		if (GET_SPEED(arg) == CODEC) {
			if (dev)
				SETS(sync_serial_prescale_shadow,
					R_SYNC_SERIAL_PRESCALE, clk_sel_u3,
					codec);
			else
				SETS(sync_serial_prescale_shadow,
					R_SYNC_SERIAL_PRESCALE, clk_sel_u1,
					codec);

			SETF(sync_serial_prescale_shadow,
				R_SYNC_SERIAL_PRESCALE, prescaler,
				GET_FREQ(arg));
			SETF(sync_serial_prescale_shadow,
				R_SYNC_SERIAL_PRESCALE, frame_rate,
				GET_FRAME_RATE(arg));
			SETF(sync_serial_prescale_shadow,
				R_SYNC_SERIAL_PRESCALE, word_rate,
				GET_WORD_RATE(arg));
		} else {
			SETF(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL,
				tr_baud, GET_SPEED(arg));
			if (dev)
				SETS(sync_serial_prescale_shadow,
					R_SYNC_SERIAL_PRESCALE, clk_sel_u3,
					baudrate);
			else
				SETS(sync_serial_prescale_shadow,
					R_SYNC_SERIAL_PRESCALE, clk_sel_u1,
					baudrate);
		}
		break;
	case SSP_MODE:
		if (arg > 5)
			return -EINVAL;
		if (arg == MASTER_OUTPUT || arg == SLAVE_OUTPUT)
			*R_IRQ_MASK1_CLR = 1 << port->data_avail_bit;
		else if (!port->use_dma)
			*R_IRQ_MASK1_SET = 1 << port->data_avail_bit;
		SETF(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL, mode, arg);
		break;
	case SSP_FRAME_SYNC:
		if (arg & NORMAL_SYNC)
			SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL,
				f_synctype, normal);
		else if (arg & EARLY_SYNC)
			SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL,
				f_synctype, early);

		if (arg & BIT_SYNC)
			SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL,
				f_syncsize, bit);
		else if (arg & WORD_SYNC)
			SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL,
				f_syncsize, word);
		else if (arg & EXTENDED_SYNC)
			SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL,
				f_syncsize, extended);

		if (arg & SYNC_ON)
			SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL,
				f_sync, on);
		else if (arg & SYNC_OFF)
			SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL,
				f_sync, off);

		if (arg & WORD_SIZE_8)
			SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL,
				wordsize, size8bit);
		else if (arg & WORD_SIZE_12)
			SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL,
				wordsize, size12bit);
		else if (arg & WORD_SIZE_16)
			SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL,
				wordsize, size16bit);
		else if (arg & WORD_SIZE_24)
			SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL,
				wordsize, size24bit);
		else if (arg & WORD_SIZE_32)
			SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL,
				wordsize, size32bit);

		if (arg & BIT_ORDER_MSB)
			SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL,
				bitorder, msb);
		else if (arg & BIT_ORDER_LSB)
			SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL,
				bitorder, lsb);

		if (arg & FLOW_CONTROL_ENABLE)
			SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL,
				flow_ctrl, enabled);
		else if (arg & FLOW_CONTROL_DISABLE)
			SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL,
				flow_ctrl, disabled);

		if (arg & CLOCK_NOT_GATED)
			SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL,
				clk_mode, normal);
		else if (arg & CLOCK_GATED)
			SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL,
				clk_mode, gated);

		break;
	case SSP_IPOLARITY:
		/* NOTE!! negedge is considered NORMAL */
		if (arg & CLOCK_NORMAL)
			SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL,
				clk_polarity, neg);
		else if (arg & CLOCK_INVERT)
			SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL,
				clk_polarity, pos);

		if (arg & FRAME_NORMAL)
			SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL,
				frame_polarity, normal);
		else if (arg & FRAME_INVERT)
			SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL,
				frame_polarity, inverted);

		if (arg & STATUS_NORMAL)
			SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL,
				status_polarity, normal);
		else if (arg & STATUS_INVERT)
			SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL,
				status_polarity, inverted);
		break;
	case SSP_OPOLARITY:
		if (arg & CLOCK_NORMAL)
			SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL,
				clk_driver, normal);
		else if (arg & CLOCK_INVERT)
			SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL,
				clk_driver, inverted);

		if (arg & FRAME_NORMAL)
			SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL,
				frame_driver, normal);
		else if (arg & FRAME_INVERT)
			SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL,
				frame_driver, inverted);

		if (arg & STATUS_NORMAL)
			SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL,
				status_driver, normal);
		else if (arg & STATUS_INVERT)
			SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL,
				status_driver, inverted);
		break;
	case SSP_SPI:
		SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL, flow_ctrl,
			disabled);
		SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL, bitorder,
			msb);
		SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL, wordsize,
			size8bit);
		SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL, f_sync, on);
		SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL, f_syncsize,
			word);
		SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL, f_synctype,
			normal);
		if (arg & SPI_SLAVE) {
			SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL,
				frame_polarity, inverted);
			SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL,
				clk_polarity, neg);
			SETF(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL,
				mode, SLAVE_INPUT);
		} else {
			SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL,
				frame_driver, inverted);
			SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL,
				clk_driver, inverted);
			SETF(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL,
				mode, MASTER_OUTPUT);
		}
		break;
	case SSP_INBUFCHUNK:
#if 0
		if (arg > port->in_buffer_size/NUM_IN_DESCR)
			return -EINVAL;
		port->inbufchunk = arg;
		/* Make sure in_buffer_size is a multiple of inbufchunk */
		port->in_buffer_size =
			(port->in_buffer_size/port->inbufchunk) *
			port->inbufchunk;
		DEBUG(printk(KERN_DEBUG "inbufchunk %i in_buffer_size: %i\n",
			port->inbufchunk, port->in_buffer_size));
		if (port->use_dma) {
			if (port->port_nbr == 0) {
				RESET_DMA(9);
				WAIT_DMA(9);
			} else {
				RESET_DMA(5);
				WAIT_DMA(5);
			}
			start_dma_in(port);
		}
#endif
		break;
	default:
		return_val = -1;
	}
	/* Make sure we write the config without interruption */
	local_irq_save(flags);
	/* Set config and enable port */
	*port->ctrl_data = port->ctrl_data_shadow;
	nop(); nop(); nop(); nop();
	*R_SYNC_SERIAL_PRESCALE = sync_serial_prescale_shadow;
	nop(); nop(); nop(); nop();
	if (dev)
		SETS(gen_config_ii_shadow, R_GEN_CONFIG_II, sermode3, sync);
	else
		SETS(gen_config_ii_shadow, R_GEN_CONFIG_II, sermode1, sync);

	*R_GEN_CONFIG_II = gen_config_ii_shadow;
	/* Reset DMA. At readout from serial port the data could be shifted
	 * one byte if not resetting DMA.
	 */
	if (port->use_dma) {
		if (port->port_nbr == 0) {
			RESET_DMA(9);
			WAIT_DMA(9);
		} else {
			RESET_DMA(5);
			WAIT_DMA(5);
		}
		start_dma_in(port);
	}
	local_irq_restore(flags);
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


static ssize_t sync_serial_write(struct file *file, const char *buf,
	size_t count, loff_t *ppos)
{
	int dev = MINOR(file_inode(file)->i_rdev);
	DECLARE_WAITQUEUE(wait, current);
	struct sync_port *port;
	unsigned long flags;
	unsigned long c, c1;
	unsigned long free_outp;
	unsigned long outp;
	unsigned long out_buffer;

	if (dev < 0 || dev >= NUMBER_OF_PORTS || !ports[dev].enabled) {
		DEBUG(printk(KERN_DEBUG "Invalid minor %d\n", dev));
		return -ENODEV;
	}
	port = &ports[dev];

	DEBUGWRITE(printk(KERN_DEBUG "W d%d c %lu (%d/%d)\n",
		port->port_nbr, count, port->out_count, OUT_BUFFER_SIZE));
	/* Space to end of buffer */
	/*
	 * out_buffer <c1>012345<-   c    ->OUT_BUFFER_SIZE
	 *            outp^    +out_count
	 *                      ^free_outp
	 * out_buffer 45<-     c      ->0123OUT_BUFFER_SIZE
	 *             +out_count   outp^
	 *              free_outp
	 *
	 */

	/* Read variables that may be updated by interrupts */
	local_irq_save(flags);
	if (count > OUT_BUFFER_SIZE - port->out_count)
		count = OUT_BUFFER_SIZE - port->out_count;

	outp = (unsigned long)port->outp;
	free_outp = outp + port->out_count;
	local_irq_restore(flags);
	out_buffer = (unsigned long)port->out_buffer;

	/* Find out where and how much to write */
	if (free_outp >= out_buffer + OUT_BUFFER_SIZE)
		free_outp -= OUT_BUFFER_SIZE;
	if (free_outp >= outp)
		c = out_buffer + OUT_BUFFER_SIZE - free_outp;
	else
		c = outp - free_outp;
	if (c > count)
		c = count;

	DEBUGWRITE(printk(KERN_DEBUG "w op %08lX fop %08lX c %lu\n",
		outp, free_outp, c));
	if (copy_from_user((void *)free_outp, buf, c))
		return -EFAULT;

	if (c != count) {
		buf += c;
		c1 = count - c;
		DEBUGWRITE(printk(KERN_DEBUG "w2 fi %lu c %lu c1 %lu\n",
			free_outp-out_buffer, c, c1));
		if (copy_from_user((void *)out_buffer, buf, c1))
			return -EFAULT;
	}
	local_irq_save(flags);
	port->out_count += count;
	local_irq_restore(flags);

	/* Make sure transmitter/receiver is running */
	if (!port->started) {
		SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL, clk_halt,
			running);
		SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL, tr_enable,
			enable);
		SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL, rec_enable,
			enable);
		port->started = 1;
	}

	*port->ctrl_data = port->ctrl_data_shadow;

	if (file->f_flags & O_NONBLOCK)	{
		local_irq_save(flags);
		if (!port->tr_running) {
			if (!port->use_dma) {
				/* Start sender by writing data */
				send_word(port);
				/* and enable transmitter ready IRQ */
				*R_IRQ_MASK1_SET = 1 <<
					port->transmitter_ready_bit;
			} else
				start_dma(port,
					(unsigned char *volatile)port->outp, c);
		}
		local_irq_restore(flags);
		DEBUGWRITE(printk(KERN_DEBUG "w d%d c %lu NB\n",
			port->port_nbr, count));
		return count;
	}

	/* Sleep until all sent */
	add_wait_queue(&port->out_wait_q, &wait);
	set_current_state(TASK_INTERRUPTIBLE);
	local_irq_save(flags);
	if (!port->tr_running) {
		if (!port->use_dma) {
			/* Start sender by writing data */
			send_word(port);
			/* and enable transmitter ready IRQ */
			*R_IRQ_MASK1_SET = 1 << port->transmitter_ready_bit;
		} else
			start_dma(port, port->outp, c);
	}
	local_irq_restore(flags);
	schedule();
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&port->out_wait_q, &wait);
	if (signal_pending(current))
		return -EINTR;

	DEBUGWRITE(printk(KERN_DEBUG "w d%d c %lu\n", port->port_nbr, count));
	return count;
}

static ssize_t sync_serial_read(struct file *file, char *buf,
				size_t count, loff_t *ppos)
{
	int dev = MINOR(file_inode(file)->i_rdev);
	int avail;
	struct sync_port *port;
	unsigned char *start;
	unsigned char *end;
	unsigned long flags;

	if (dev < 0 || dev >= NUMBER_OF_PORTS || !ports[dev].enabled) {
		DEBUG(printk(KERN_DEBUG "Invalid minor %d\n", dev));
		return -ENODEV;
	}
	port = &ports[dev];

	DEBUGREAD(printk(KERN_DEBUG "R%d c %d ri %lu wi %lu /%lu\n",
		dev, count, port->readp - port->flip,
		port->writep - port->flip, port->in_buffer_size));

	if (!port->started) {
		SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL, clk_halt,
			running);
		SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL, tr_enable,
			enable);
		SETS(port->ctrl_data_shadow, R_SYNC_SERIAL1_CTRL, rec_enable,
			enable);
		port->started = 1;
	}
	*port->ctrl_data = port->ctrl_data_shadow;

	/* Calculate number of available bytes */
	/* Save pointers to avoid that they are modified by interrupt */
	local_irq_save(flags);
	start = (unsigned char *)port->readp; /* cast away volatile */
	end = (unsigned char *)port->writep;  /* cast away volatile */
	local_irq_restore(flags);
	while (start == end && !port->full) {
		/* No data */
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;

		wait_event_interruptible(port->in_wait_q,
					 !(start == end && !port->full));
		if (signal_pending(current))
			return -EINTR;

		local_irq_save(flags);
		start = (unsigned char *)port->readp; /* cast away volatile */
		end = (unsigned char *)port->writep;  /* cast away volatile */
		local_irq_restore(flags);
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
	local_irq_save(flags);
	port->readp += count;
	if (port->readp >= port->flip + port->in_buffer_size) /* Wrap? */
		port->readp = port->flip;
	port->full = 0;
	local_irq_restore(flags);
	DEBUGREAD(printk(KERN_DEBUG "r %d\n", count));
	return count;
}

static void send_word(struct sync_port *port)
{
	switch (IO_EXTRACT(R_SYNC_SERIAL1_CTRL, wordsize,
			port->ctrl_data_shadow)) {
	case IO_STATE_VALUE(R_SYNC_SERIAL1_CTRL, wordsize, size8bit):
		 port->out_count--;
		 *port->data_out = *port->outp++;
		 if (port->outp >= port->out_buffer + OUT_BUFFER_SIZE)
			 port->outp = port->out_buffer;
		 break;
	case IO_STATE_VALUE(R_SYNC_SERIAL1_CTRL, wordsize, size12bit):
	{
		int data = (*port->outp++) << 8;
		data |= *port->outp++;
		port->out_count -= 2;
		*port->data_out = data;
		if (port->outp >= port->out_buffer + OUT_BUFFER_SIZE)
			port->outp = port->out_buffer;
		break;
	}
	case IO_STATE_VALUE(R_SYNC_SERIAL1_CTRL, wordsize, size16bit):
		port->out_count -= 2;
		*port->data_out = *(unsigned short *)port->outp;
		port->outp += 2;
		if (port->outp >= port->out_buffer + OUT_BUFFER_SIZE)
			port->outp = port->out_buffer;
		break;
	case IO_STATE_VALUE(R_SYNC_SERIAL1_CTRL, wordsize, size24bit):
		port->out_count -= 3;
		*port->data_out = *(unsigned int *)port->outp;
		port->outp += 3;
		if (port->outp >= port->out_buffer + OUT_BUFFER_SIZE)
			port->outp = port->out_buffer;
		break;
	case IO_STATE_VALUE(R_SYNC_SERIAL1_CTRL, wordsize, size32bit):
		port->out_count -= 4;
		*port->data_out = *(unsigned int *)port->outp;
		port->outp += 4;
		if (port->outp >= port->out_buffer + OUT_BUFFER_SIZE)
			port->outp = port->out_buffer;
		break;
	}
}


static void start_dma(struct sync_port *port, const char *data, int count)
{
	port->tr_running = 1;
	port->out_descr.hw_len = 0;
	port->out_descr.next = 0;
	port->out_descr.ctrl = d_eol | d_eop; /* No d_wait to avoid glitches */
	port->out_descr.sw_len = count;
	port->out_descr.buf = virt_to_phys(data);
	port->out_descr.status = 0;

	*port->output_dma_first = virt_to_phys(&port->out_descr);
	*port->output_dma_cmd = IO_STATE(R_DMA_CH0_CMD, cmd, start);
	DEBUGTXINT(printk(KERN_DEBUG "dma %08lX c %d\n",
		(unsigned long)data, count));
}

static void start_dma_in(struct sync_port *port)
{
	int i;
	unsigned long buf;
	port->writep = port->flip;

	if (port->writep > port->flip + port->in_buffer_size) {
		panic("Offset too large in sync serial driver\n");
		return;
	}
	buf = virt_to_phys(port->in_buffer);
	for (i = 0; i < NUM_IN_DESCR; i++) {
		port->in_descr[i].sw_len = port->inbufchunk;
		port->in_descr[i].ctrl = d_int;
		port->in_descr[i].next = virt_to_phys(&port->in_descr[i+1]);
		port->in_descr[i].buf = buf;
		port->in_descr[i].hw_len = 0;
		port->in_descr[i].status = 0;
		port->in_descr[i].fifo_len = 0;
		buf += port->inbufchunk;
		prepare_rx_descriptor(&port->in_descr[i]);
	}
	/* Link the last descriptor to the first */
	port->in_descr[i-1].next = virt_to_phys(&port->in_descr[0]);
	port->in_descr[i-1].ctrl |= d_eol;
	port->next_rx_desc = &port->in_descr[0];
	port->prev_rx_desc = &port->in_descr[NUM_IN_DESCR - 1];
	*port->input_dma_first = virt_to_phys(port->next_rx_desc);
	*port->input_dma_cmd = IO_STATE(R_DMA_CH0_CMD, cmd, start);
}

#ifdef SYNC_SER_DMA
static irqreturn_t tr_interrupt(int irq, void *dev_id)
{
	unsigned long ireg = *R_IRQ_MASK2_RD;
	struct etrax_dma_descr *descr;
	unsigned int sentl;
	int handled = 0;
	int i;

	for (i = 0; i < NUMBER_OF_PORTS; i++) {
		struct sync_port *port = &ports[i];
		if (!port->enabled  || !port->use_dma)
			continue;

		/* IRQ active for the port? */
		if (!(ireg & (1 << port->output_dma_bit)))
			continue;

		handled = 1;

		/* Clear IRQ */
		*port->output_dma_clr_irq =
			IO_STATE(R_DMA_CH0_CLR_INTR, clr_eop, do) |
			IO_STATE(R_DMA_CH0_CLR_INTR, clr_descr, do);

		descr = &port->out_descr;
		if (!(descr->status & d_stop))
			sentl = descr->sw_len;
		else
			/* Otherwise find amount of data sent here */
			sentl = descr->hw_len;

		port->out_count -= sentl;
		port->outp += sentl;
		if (port->outp >= port->out_buffer + OUT_BUFFER_SIZE)
			port->outp = port->out_buffer;
		if (port->out_count) {
			int c = port->out_buffer + OUT_BUFFER_SIZE - port->outp;
			if (c > port->out_count)
				c = port->out_count;
			DEBUGTXINT(printk(KERN_DEBUG
				"tx_int DMAWRITE %i %i\n", sentl, c));
			start_dma(port, port->outp, c);
		} else  {
			DEBUGTXINT(printk(KERN_DEBUG
				"tx_int DMA stop %i\n", sentl));
			port->tr_running = 0;
		}
		/* wake up the waiting process */
		wake_up_interruptible(&port->out_wait_q);
	}
	return IRQ_RETVAL(handled);
} /* tr_interrupt */

static irqreturn_t rx_interrupt(int irq, void *dev_id)
{
	unsigned long ireg = *R_IRQ_MASK2_RD;
	int i;
	int handled = 0;

	for (i = 0; i < NUMBER_OF_PORTS; i++) {
		struct sync_port *port = &ports[i];

		if (!port->enabled || !port->use_dma)
			continue;

		if (!(ireg & (1 << port->input_dma_descr_bit)))
			continue;

		/* Descriptor interrupt */
		handled = 1;
		while (*port->input_dma_descr !=
				virt_to_phys(port->next_rx_desc)) {
			if (port->writep + port->inbufchunk > port->flip +
					port->in_buffer_size) {
				int first_size = port->flip +
					port->in_buffer_size - port->writep;
				memcpy(port->writep,
					phys_to_virt(port->next_rx_desc->buf),
					first_size);
				memcpy(port->flip,
					phys_to_virt(port->next_rx_desc->buf +
					first_size),
					port->inbufchunk - first_size);
				port->writep = port->flip +
					port->inbufchunk - first_size;
			} else {
				memcpy(port->writep,
					phys_to_virt(port->next_rx_desc->buf),
					port->inbufchunk);
				port->writep += port->inbufchunk;
				if (port->writep >= port->flip
						+ port->in_buffer_size)
					port->writep = port->flip;
			}
			if (port->writep == port->readp)
				port->full = 1;
			prepare_rx_descriptor(port->next_rx_desc);
			port->next_rx_desc->ctrl |= d_eol;
			port->prev_rx_desc->ctrl &= ~d_eol;
			port->prev_rx_desc = phys_to_virt((unsigned)
				port->next_rx_desc);
			port->next_rx_desc = phys_to_virt((unsigned)
				port->next_rx_desc->next);
			/* Wake up the waiting process */
			wake_up_interruptible(&port->in_wait_q);
			*port->input_dma_cmd = IO_STATE(R_DMA_CH1_CMD,
				cmd, restart);
			/* DMA has reached end of descriptor */
			*port->input_dma_clr_irq = IO_STATE(R_DMA_CH0_CLR_INTR,
				clr_descr, do);
		}
	}
	return IRQ_RETVAL(handled);
} /* rx_interrupt */
#endif /* SYNC_SER_DMA */

#ifdef SYNC_SER_MANUAL
static irqreturn_t manual_interrupt(int irq, void *dev_id)
{
	int i;
	int handled = 0;

	for (i = 0; i < NUMBER_OF_PORTS; i++) {
		struct sync_port *port = &ports[i];

		if (!port->enabled || port->use_dma)
			continue;

		/* Data received? */
		if (*R_IRQ_MASK1_RD & (1 << port->data_avail_bit)) {
			handled = 1;
			/* Read data */
			switch (port->ctrl_data_shadow &
				IO_MASK(R_SYNC_SERIAL1_CTRL, wordsize)) {
			case IO_STATE(R_SYNC_SERIAL1_CTRL, wordsize, size8bit):
				*port->writep++ =
					*(volatile char *)port->data_in;
				break;
			case IO_STATE(R_SYNC_SERIAL1_CTRL, wordsize, size12bit):
			{
				int data = *(unsigned short *)port->data_in;
				*port->writep = (data & 0x0ff0) >> 4;
				*(port->writep + 1) = data & 0x0f;
				port->writep += 2;
				break;
			}
			case IO_STATE(R_SYNC_SERIAL1_CTRL, wordsize, size16bit):
				*(unsigned short *)port->writep =
					*(volatile unsigned short *)port->data_in;
				port->writep += 2;
				break;
			case IO_STATE(R_SYNC_SERIAL1_CTRL, wordsize, size24bit):
				*(unsigned int *)port->writep = *port->data_in;
				port->writep += 3;
				break;
			case IO_STATE(R_SYNC_SERIAL1_CTRL, wordsize, size32bit):
				*(unsigned int *)port->writep = *port->data_in;
				port->writep += 4;
				break;
			}

			/* Wrap? */
			if (port->writep >= port->flip + port->in_buffer_size)
				port->writep = port->flip;
			if (port->writep == port->readp) {
				/* Receive buffer overrun, discard oldest */
				port->readp++;
				/* Wrap? */
				if (port->readp >= port->flip +
						port->in_buffer_size)
					port->readp = port->flip;
			}
			if (sync_data_avail(port) >= port->inbufchunk) {
				/* Wake up application */
				wake_up_interruptible(&port->in_wait_q);
			}
		}

		/* Transmitter ready? */
		if (*R_IRQ_MASK1_RD & (1 << port->transmitter_ready_bit)) {
			if (port->out_count > 0) {
				/* More data to send */
				send_word(port);
			} else {
				/* Transmission finished */
				/* Turn off IRQ */
				*R_IRQ_MASK1_CLR = 1 <<
					port->transmitter_ready_bit;
				/* Wake up application */
				wake_up_interruptible(&port->out_wait_q);
			}
		}
	}
	return IRQ_RETVAL(handled);
}
#endif

module_init(etrax_sync_serial_init);
