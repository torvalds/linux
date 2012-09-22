/*
 * $Id: synclinkmp.c,v 4.38 2005/07/15 13:29:44 paulkf Exp $
 *
 * Device driver for Microgate SyncLink Multiport
 * high speed multiprotocol serial adapter.
 *
 * written by Paul Fulghum for Microgate Corporation
 * paulkf@microgate.com
 *
 * Microgate and SyncLink are trademarks of Microgate Corporation
 *
 * Derived from serial.c written by Theodore Ts'o and Linus Torvalds
 * This code is released under the GNU General Public License (GPL)
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define VERSION(ver,rel,seq) (((ver)<<16) | ((rel)<<8) | (seq))
#if defined(__i386__)
#  define BREAKPOINT() asm("   int $3");
#else
#  define BREAKPOINT() { }
#endif

#define MAX_DEVICES 12

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/fcntl.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/mm.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/netdevice.h>
#include <linux/vmalloc.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/ioctl.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/dma.h>
#include <linux/bitops.h>
#include <asm/types.h>
#include <linux/termios.h>
#include <linux/workqueue.h>
#include <linux/hdlc.h>
#include <linux/synclink.h>

#if defined(CONFIG_HDLC) || (defined(CONFIG_HDLC_MODULE) && defined(CONFIG_SYNCLINKMP_MODULE))
#define SYNCLINK_GENERIC_HDLC 1
#else
#define SYNCLINK_GENERIC_HDLC 0
#endif

#define GET_USER(error,value,addr) error = get_user(value,addr)
#define COPY_FROM_USER(error,dest,src,size) error = copy_from_user(dest,src,size) ? -EFAULT : 0
#define PUT_USER(error,value,addr) error = put_user(value,addr)
#define COPY_TO_USER(error,dest,src,size) error = copy_to_user(dest,src,size) ? -EFAULT : 0

#include <asm/uaccess.h>

static MGSL_PARAMS default_params = {
	MGSL_MODE_HDLC,			/* unsigned long mode */
	0,				/* unsigned char loopback; */
	HDLC_FLAG_UNDERRUN_ABORT15,	/* unsigned short flags; */
	HDLC_ENCODING_NRZI_SPACE,	/* unsigned char encoding; */
	0,				/* unsigned long clock_speed; */
	0xff,				/* unsigned char addr_filter; */
	HDLC_CRC_16_CCITT,		/* unsigned short crc_type; */
	HDLC_PREAMBLE_LENGTH_8BITS,	/* unsigned char preamble_length; */
	HDLC_PREAMBLE_PATTERN_NONE,	/* unsigned char preamble; */
	9600,				/* unsigned long data_rate; */
	8,				/* unsigned char data_bits; */
	1,				/* unsigned char stop_bits; */
	ASYNC_PARITY_NONE		/* unsigned char parity; */
};

/* size in bytes of DMA data buffers */
#define SCABUFSIZE 	1024
#define SCA_MEM_SIZE	0x40000
#define SCA_BASE_SIZE   512
#define SCA_REG_SIZE    16
#define SCA_MAX_PORTS   4
#define SCAMAXDESC 	128

#define	BUFFERLISTSIZE	4096

/* SCA-I style DMA buffer descriptor */
typedef struct _SCADESC
{
	u16	next;		/* lower l6 bits of next descriptor addr */
	u16	buf_ptr;	/* lower 16 bits of buffer addr */
	u8	buf_base;	/* upper 8 bits of buffer addr */
	u8	pad1;
	u16	length;		/* length of buffer */
	u8	status;		/* status of buffer */
	u8	pad2;
} SCADESC, *PSCADESC;

typedef struct _SCADESC_EX
{
	/* device driver bookkeeping section */
	char 	*virt_addr;    	/* virtual address of data buffer */
	u16	phys_entry;	/* lower 16-bits of physical address of this descriptor */
} SCADESC_EX, *PSCADESC_EX;

/* The queue of BH actions to be performed */

#define BH_RECEIVE  1
#define BH_TRANSMIT 2
#define BH_STATUS   4

#define IO_PIN_SHUTDOWN_LIMIT 100

struct	_input_signal_events {
	int	ri_up;
	int	ri_down;
	int	dsr_up;
	int	dsr_down;
	int	dcd_up;
	int	dcd_down;
	int	cts_up;
	int	cts_down;
};

/*
 * Device instance data structure
 */
typedef struct _synclinkmp_info {
	void *if_ptr;				/* General purpose pointer (used by SPPP) */
	int			magic;
	struct tty_port		port;
	int			line;
	unsigned short		close_delay;
	unsigned short		closing_wait;	/* time to wait before closing */

	struct mgsl_icount	icount;

	int			timeout;
	int			x_char;		/* xon/xoff character */
	u16			read_status_mask1;  /* break detection (SR1 indications) */
	u16			read_status_mask2;  /* parity/framing/overun (SR2 indications) */
	unsigned char 		ignore_status_mask1;  /* break detection (SR1 indications) */
	unsigned char		ignore_status_mask2;  /* parity/framing/overun (SR2 indications) */
	unsigned char 		*tx_buf;
	int			tx_put;
	int			tx_get;
	int			tx_count;

	wait_queue_head_t	status_event_wait_q;
	wait_queue_head_t	event_wait_q;
	struct timer_list	tx_timer;	/* HDLC transmit timeout timer */
	struct _synclinkmp_info	*next_device;	/* device list link */
	struct timer_list	status_timer;	/* input signal status check timer */

	spinlock_t lock;		/* spinlock for synchronizing with ISR */
	struct work_struct task;	 		/* task structure for scheduling bh */

	u32 max_frame_size;			/* as set by device config */

	u32 pending_bh;

	bool bh_running;				/* Protection from multiple */
	int isr_overflow;
	bool bh_requested;

	int dcd_chkcount;			/* check counts to prevent */
	int cts_chkcount;			/* too many IRQs if a signal */
	int dsr_chkcount;			/* is floating */
	int ri_chkcount;

	char *buffer_list;			/* virtual address of Rx & Tx buffer lists */
	unsigned long buffer_list_phys;

	unsigned int rx_buf_count;		/* count of total allocated Rx buffers */
	SCADESC *rx_buf_list;   		/* list of receive buffer entries */
	SCADESC_EX rx_buf_list_ex[SCAMAXDESC]; /* list of receive buffer entries */
	unsigned int current_rx_buf;

	unsigned int tx_buf_count;		/* count of total allocated Tx buffers */
	SCADESC *tx_buf_list;		/* list of transmit buffer entries */
	SCADESC_EX tx_buf_list_ex[SCAMAXDESC]; /* list of transmit buffer entries */
	unsigned int last_tx_buf;

	unsigned char *tmp_rx_buf;
	unsigned int tmp_rx_buf_count;

	bool rx_enabled;
	bool rx_overflow;

	bool tx_enabled;
	bool tx_active;
	u32 idle_mode;

	unsigned char ie0_value;
	unsigned char ie1_value;
	unsigned char ie2_value;
	unsigned char ctrlreg_value;
	unsigned char old_signals;

	char device_name[25];			/* device instance name */

	int port_count;
	int adapter_num;
	int port_num;

	struct _synclinkmp_info *port_array[SCA_MAX_PORTS];

	unsigned int bus_type;			/* expansion bus type (ISA,EISA,PCI) */

	unsigned int irq_level;			/* interrupt level */
	unsigned long irq_flags;
	bool irq_requested;			/* true if IRQ requested */

	MGSL_PARAMS params;			/* communications parameters */

	unsigned char serial_signals;		/* current serial signal states */

	bool irq_occurred;			/* for diagnostics use */
	unsigned int init_error;		/* Initialization startup error */

	u32 last_mem_alloc;
	unsigned char* memory_base;		/* shared memory address (PCI only) */
	u32 phys_memory_base;
    	int shared_mem_requested;

	unsigned char* sca_base;		/* HD64570 SCA Memory address */
	u32 phys_sca_base;
	u32 sca_offset;
	bool sca_base_requested;

	unsigned char* lcr_base;		/* local config registers (PCI only) */
	u32 phys_lcr_base;
	u32 lcr_offset;
	int lcr_mem_requested;

	unsigned char* statctrl_base;		/* status/control register memory */
	u32 phys_statctrl_base;
	u32 statctrl_offset;
	bool sca_statctrl_requested;

	u32 misc_ctrl_value;
	char flag_buf[MAX_ASYNC_BUFFER_SIZE];
	char char_buf[MAX_ASYNC_BUFFER_SIZE];
	bool drop_rts_on_tx_done;

	struct	_input_signal_events	input_signal_events;

	/* SPPP/Cisco HDLC device parts */
	int netcount;
	spinlock_t netlock;

#if SYNCLINK_GENERIC_HDLC
	struct net_device *netdev;
#endif

} SLMP_INFO;

#define MGSL_MAGIC 0x5401

/*
 * define serial signal status change macros
 */
#define	MISCSTATUS_DCD_LATCHED	(SerialSignal_DCD<<8)	/* indicates change in DCD */
#define MISCSTATUS_RI_LATCHED	(SerialSignal_RI<<8)	/* indicates change in RI */
#define MISCSTATUS_CTS_LATCHED	(SerialSignal_CTS<<8)	/* indicates change in CTS */
#define MISCSTATUS_DSR_LATCHED	(SerialSignal_DSR<<8)	/* change in DSR */

/* Common Register macros */
#define LPR	0x00
#define PABR0	0x02
#define PABR1	0x03
#define WCRL	0x04
#define WCRM	0x05
#define WCRH	0x06
#define DPCR	0x08
#define DMER	0x09
#define ISR0	0x10
#define ISR1	0x11
#define ISR2	0x12
#define IER0	0x14
#define IER1	0x15
#define IER2	0x16
#define ITCR	0x18
#define INTVR 	0x1a
#define IMVR	0x1c

/* MSCI Register macros */
#define TRB	0x20
#define TRBL	0x20
#define TRBH	0x21
#define SR0	0x22
#define SR1	0x23
#define SR2	0x24
#define SR3	0x25
#define FST	0x26
#define IE0	0x28
#define IE1	0x29
#define IE2	0x2a
#define FIE	0x2b
#define CMD	0x2c
#define MD0	0x2e
#define MD1	0x2f
#define MD2	0x30
#define CTL	0x31
#define SA0	0x32
#define SA1	0x33
#define IDL	0x34
#define TMC	0x35
#define RXS	0x36
#define TXS	0x37
#define TRC0	0x38
#define TRC1	0x39
#define RRC	0x3a
#define CST0	0x3c
#define CST1	0x3d

/* Timer Register Macros */
#define TCNT	0x60
#define TCNTL	0x60
#define TCNTH	0x61
#define TCONR	0x62
#define TCONRL	0x62
#define TCONRH	0x63
#define TMCS	0x64
#define TEPR	0x65

/* DMA Controller Register macros */
#define DARL	0x80
#define DARH	0x81
#define DARB	0x82
#define BAR	0x80
#define BARL	0x80
#define BARH	0x81
#define BARB	0x82
#define SAR	0x84
#define SARL	0x84
#define SARH	0x85
#define SARB	0x86
#define CPB	0x86
#define CDA	0x88
#define CDAL	0x88
#define CDAH	0x89
#define EDA	0x8a
#define EDAL	0x8a
#define EDAH	0x8b
#define BFL	0x8c
#define BFLL	0x8c
#define BFLH	0x8d
#define BCR	0x8e
#define BCRL	0x8e
#define BCRH	0x8f
#define DSR	0x90
#define DMR	0x91
#define FCT	0x93
#define DIR	0x94
#define DCMD	0x95

/* combine with timer or DMA register address */
#define TIMER0	0x00
#define TIMER1	0x08
#define TIMER2	0x10
#define TIMER3	0x18
#define RXDMA 	0x00
#define TXDMA 	0x20

/* SCA Command Codes */
#define NOOP		0x00
#define TXRESET		0x01
#define TXENABLE	0x02
#define TXDISABLE	0x03
#define TXCRCINIT	0x04
#define TXCRCEXCL	0x05
#define TXEOM		0x06
#define TXABORT		0x07
#define MPON		0x08
#define TXBUFCLR	0x09
#define RXRESET		0x11
#define RXENABLE	0x12
#define RXDISABLE	0x13
#define RXCRCINIT	0x14
#define RXREJECT	0x15
#define SEARCHMP	0x16
#define RXCRCEXCL	0x17
#define RXCRCCALC	0x18
#define CHRESET		0x21
#define HUNT		0x31

/* DMA command codes */
#define SWABORT		0x01
#define FEICLEAR	0x02

/* IE0 */
#define TXINTE 		BIT7
#define RXINTE 		BIT6
#define TXRDYE 		BIT1
#define RXRDYE 		BIT0

/* IE1 & SR1 */
#define UDRN   	BIT7
#define IDLE   	BIT6
#define SYNCD  	BIT4
#define FLGD   	BIT4
#define CCTS   	BIT3
#define CDCD   	BIT2
#define BRKD   	BIT1
#define ABTD   	BIT1
#define GAPD   	BIT1
#define BRKE   	BIT0
#define IDLD	BIT0

/* IE2 & SR2 */
#define EOM	BIT7
#define PMP	BIT6
#define SHRT	BIT6
#define PE	BIT5
#define ABT	BIT5
#define FRME	BIT4
#define RBIT	BIT4
#define OVRN	BIT3
#define CRCE	BIT2


/*
 * Global linked list of SyncLink devices
 */
static SLMP_INFO *synclinkmp_device_list = NULL;
static int synclinkmp_adapter_count = -1;
static int synclinkmp_device_count = 0;

/*
 * Set this param to non-zero to load eax with the
 * .text section address and breakpoint on module load.
 * This is useful for use with gdb and add-symbol-file command.
 */
static bool break_on_load = 0;

/*
 * Driver major number, defaults to zero to get auto
 * assigned major number. May be forced as module parameter.
 */
static int ttymajor = 0;

/*
 * Array of user specified options for ISA adapters.
 */
static int debug_level = 0;
static int maxframe[MAX_DEVICES] = {0,};

module_param(break_on_load, bool, 0);
module_param(ttymajor, int, 0);
module_param(debug_level, int, 0);
module_param_array(maxframe, int, NULL, 0);

static char *driver_name = "SyncLink MultiPort driver";
static char *driver_version = "$Revision: 4.38 $";

static int synclinkmp_init_one(struct pci_dev *dev,const struct pci_device_id *ent);
static void synclinkmp_remove_one(struct pci_dev *dev);

static struct pci_device_id synclinkmp_pci_tbl[] = {
	{ PCI_VENDOR_ID_MICROGATE, PCI_DEVICE_ID_MICROGATE_SCA, PCI_ANY_ID, PCI_ANY_ID, },
	{ 0, }, /* terminate list */
};
MODULE_DEVICE_TABLE(pci, synclinkmp_pci_tbl);

MODULE_LICENSE("GPL");

static struct pci_driver synclinkmp_pci_driver = {
	.name		= "synclinkmp",
	.id_table	= synclinkmp_pci_tbl,
	.probe		= synclinkmp_init_one,
	.remove		= __devexit_p(synclinkmp_remove_one),
};


static struct tty_driver *serial_driver;

/* number of characters left in xmit buffer before we ask for more */
#define WAKEUP_CHARS 256


/* tty callbacks */

static int  open(struct tty_struct *tty, struct file * filp);
static void close(struct tty_struct *tty, struct file * filp);
static void hangup(struct tty_struct *tty);
static void set_termios(struct tty_struct *tty, struct ktermios *old_termios);

static int  write(struct tty_struct *tty, const unsigned char *buf, int count);
static int put_char(struct tty_struct *tty, unsigned char ch);
static void send_xchar(struct tty_struct *tty, char ch);
static void wait_until_sent(struct tty_struct *tty, int timeout);
static int  write_room(struct tty_struct *tty);
static void flush_chars(struct tty_struct *tty);
static void flush_buffer(struct tty_struct *tty);
static void tx_hold(struct tty_struct *tty);
static void tx_release(struct tty_struct *tty);

static int  ioctl(struct tty_struct *tty, unsigned int cmd, unsigned long arg);
static int  chars_in_buffer(struct tty_struct *tty);
static void throttle(struct tty_struct * tty);
static void unthrottle(struct tty_struct * tty);
static int set_break(struct tty_struct *tty, int break_state);

#if SYNCLINK_GENERIC_HDLC
#define dev_to_port(D) (dev_to_hdlc(D)->priv)
static void hdlcdev_tx_done(SLMP_INFO *info);
static void hdlcdev_rx(SLMP_INFO *info, char *buf, int size);
static int  hdlcdev_init(SLMP_INFO *info);
static void hdlcdev_exit(SLMP_INFO *info);
#endif

/* ioctl handlers */

static int  get_stats(SLMP_INFO *info, struct mgsl_icount __user *user_icount);
static int  get_params(SLMP_INFO *info, MGSL_PARAMS __user *params);
static int  set_params(SLMP_INFO *info, MGSL_PARAMS __user *params);
static int  get_txidle(SLMP_INFO *info, int __user *idle_mode);
static int  set_txidle(SLMP_INFO *info, int idle_mode);
static int  tx_enable(SLMP_INFO *info, int enable);
static int  tx_abort(SLMP_INFO *info);
static int  rx_enable(SLMP_INFO *info, int enable);
static int  modem_input_wait(SLMP_INFO *info,int arg);
static int  wait_mgsl_event(SLMP_INFO *info, int __user *mask_ptr);
static int  tiocmget(struct tty_struct *tty);
static int  tiocmset(struct tty_struct *tty,
			unsigned int set, unsigned int clear);
static int  set_break(struct tty_struct *tty, int break_state);

static void add_device(SLMP_INFO *info);
static void device_init(int adapter_num, struct pci_dev *pdev);
static int  claim_resources(SLMP_INFO *info);
static void release_resources(SLMP_INFO *info);

static int  startup(SLMP_INFO *info);
static int  block_til_ready(struct tty_struct *tty, struct file * filp,SLMP_INFO *info);
static int carrier_raised(struct tty_port *port);
static void shutdown(SLMP_INFO *info);
static void program_hw(SLMP_INFO *info);
static void change_params(SLMP_INFO *info);

static bool init_adapter(SLMP_INFO *info);
static bool register_test(SLMP_INFO *info);
static bool irq_test(SLMP_INFO *info);
static bool loopback_test(SLMP_INFO *info);
static int  adapter_test(SLMP_INFO *info);
static bool memory_test(SLMP_INFO *info);

static void reset_adapter(SLMP_INFO *info);
static void reset_port(SLMP_INFO *info);
static void async_mode(SLMP_INFO *info);
static void hdlc_mode(SLMP_INFO *info);

static void rx_stop(SLMP_INFO *info);
static void rx_start(SLMP_INFO *info);
static void rx_reset_buffers(SLMP_INFO *info);
static void rx_free_frame_buffers(SLMP_INFO *info, unsigned int first, unsigned int last);
static bool rx_get_frame(SLMP_INFO *info);

static void tx_start(SLMP_INFO *info);
static void tx_stop(SLMP_INFO *info);
static void tx_load_fifo(SLMP_INFO *info);
static void tx_set_idle(SLMP_INFO *info);
static void tx_load_dma_buffer(SLMP_INFO *info, const char *buf, unsigned int count);

static void get_signals(SLMP_INFO *info);
static void set_signals(SLMP_INFO *info);
static void enable_loopback(SLMP_INFO *info, int enable);
static void set_rate(SLMP_INFO *info, u32 data_rate);

static int  bh_action(SLMP_INFO *info);
static void bh_handler(struct work_struct *work);
static void bh_receive(SLMP_INFO *info);
static void bh_transmit(SLMP_INFO *info);
static void bh_status(SLMP_INFO *info);
static void isr_timer(SLMP_INFO *info);
static void isr_rxint(SLMP_INFO *info);
static void isr_rxrdy(SLMP_INFO *info);
static void isr_txint(SLMP_INFO *info);
static void isr_txrdy(SLMP_INFO *info);
static void isr_rxdmaok(SLMP_INFO *info);
static void isr_rxdmaerror(SLMP_INFO *info);
static void isr_txdmaok(SLMP_INFO *info);
static void isr_txdmaerror(SLMP_INFO *info);
static void isr_io_pin(SLMP_INFO *info, u16 status);

static int  alloc_dma_bufs(SLMP_INFO *info);
static void free_dma_bufs(SLMP_INFO *info);
static int  alloc_buf_list(SLMP_INFO *info);
static int  alloc_frame_bufs(SLMP_INFO *info, SCADESC *list, SCADESC_EX *list_ex,int count);
static int  alloc_tmp_rx_buf(SLMP_INFO *info);
static void free_tmp_rx_buf(SLMP_INFO *info);

static void load_pci_memory(SLMP_INFO *info, char* dest, const char* src, unsigned short count);
static void trace_block(SLMP_INFO *info, const char* data, int count, int xmit);
static void tx_timeout(unsigned long context);
static void status_timeout(unsigned long context);

static unsigned char read_reg(SLMP_INFO *info, unsigned char addr);
static void write_reg(SLMP_INFO *info, unsigned char addr, unsigned char val);
static u16 read_reg16(SLMP_INFO *info, unsigned char addr);
static void write_reg16(SLMP_INFO *info, unsigned char addr, u16 val);
static unsigned char read_status_reg(SLMP_INFO * info);
static void write_control_reg(SLMP_INFO * info);


static unsigned char rx_active_fifo_level = 16;	// rx request FIFO activation level in bytes
static unsigned char tx_active_fifo_level = 16;	// tx request FIFO activation level in bytes
static unsigned char tx_negate_fifo_level = 32;	// tx request FIFO negation level in bytes

static u32 misc_ctrl_value = 0x007e4040;
static u32 lcr1_brdr_value = 0x00800028;

static u32 read_ahead_count = 8;

/* DPCR, DMA Priority Control
 *
 * 07..05  Not used, must be 0
 * 04      BRC, bus release condition: 0=all transfers complete
 *              1=release after 1 xfer on all channels
 * 03      CCC, channel change condition: 0=every cycle
 *              1=after each channel completes all xfers
 * 02..00  PR<2..0>, priority 100=round robin
 *
 * 00000100 = 0x00
 */
static unsigned char dma_priority = 0x04;

// Number of bytes that can be written to shared RAM
// in a single write operation
static u32 sca_pci_load_interval = 64;

/*
 * 1st function defined in .text section. Calling this function in
 * init_module() followed by a breakpoint allows a remote debugger
 * (gdb) to get the .text address for the add-symbol-file command.
 * This allows remote debugging of dynamically loadable modules.
 */
static void* synclinkmp_get_text_ptr(void);
static void* synclinkmp_get_text_ptr(void) {return synclinkmp_get_text_ptr;}

static inline int sanity_check(SLMP_INFO *info,
			       char *name, const char *routine)
{
#ifdef SANITY_CHECK
	static const char *badmagic =
		"Warning: bad magic number for synclinkmp_struct (%s) in %s\n";
	static const char *badinfo =
		"Warning: null synclinkmp_struct for (%s) in %s\n";

	if (!info) {
		printk(badinfo, name, routine);
		return 1;
	}
	if (info->magic != MGSL_MAGIC) {
		printk(badmagic, name, routine);
		return 1;
	}
#else
	if (!info)
		return 1;
#endif
	return 0;
}

/**
 * line discipline callback wrappers
 *
 * The wrappers maintain line discipline references
 * while calling into the line discipline.
 *
 * ldisc_receive_buf  - pass receive data to line discipline
 */

static void ldisc_receive_buf(struct tty_struct *tty,
			      const __u8 *data, char *flags, int count)
{
	struct tty_ldisc *ld;
	if (!tty)
		return;
	ld = tty_ldisc_ref(tty);
	if (ld) {
		if (ld->ops->receive_buf)
			ld->ops->receive_buf(tty, data, flags, count);
		tty_ldisc_deref(ld);
	}
}

/* tty callbacks */

static int install(struct tty_driver *driver, struct tty_struct *tty)
{
	SLMP_INFO *info;
	int line = tty->index;

	if (line >= synclinkmp_device_count) {
		printk("%s(%d): open with invalid line #%d.\n",
			__FILE__,__LINE__,line);
		return -ENODEV;
	}

	info = synclinkmp_device_list;
	while (info && info->line != line)
		info = info->next_device;
	if (sanity_check(info, tty->name, "open"))
		return -ENODEV;
	if (info->init_error) {
		printk("%s(%d):%s device is not allocated, init error=%d\n",
			__FILE__, __LINE__, info->device_name,
			info->init_error);
		return -ENODEV;
	}

	tty->driver_data = info;

	return tty_port_install(&info->port, driver, tty);
}

/* Called when a port is opened.  Init and enable port.
 */
static int open(struct tty_struct *tty, struct file *filp)
{
	SLMP_INFO *info = tty->driver_data;
	unsigned long flags;
	int retval;

	info->port.tty = tty;

	if (debug_level >= DEBUG_LEVEL_INFO)
		printk("%s(%d):%s open(), old ref count = %d\n",
			 __FILE__,__LINE__,tty->driver->name, info->port.count);

	/* If port is closing, signal caller to try again */
	if (tty_hung_up_p(filp) || info->port.flags & ASYNC_CLOSING){
		if (info->port.flags & ASYNC_CLOSING)
			interruptible_sleep_on(&info->port.close_wait);
		retval = ((info->port.flags & ASYNC_HUP_NOTIFY) ?
			-EAGAIN : -ERESTARTSYS);
		goto cleanup;
	}

	info->port.tty->low_latency = (info->port.flags & ASYNC_LOW_LATENCY) ? 1 : 0;

	spin_lock_irqsave(&info->netlock, flags);
	if (info->netcount) {
		retval = -EBUSY;
		spin_unlock_irqrestore(&info->netlock, flags);
		goto cleanup;
	}
	info->port.count++;
	spin_unlock_irqrestore(&info->netlock, flags);

	if (info->port.count == 1) {
		/* 1st open on this device, init hardware */
		retval = startup(info);
		if (retval < 0)
			goto cleanup;
	}

	retval = block_til_ready(tty, filp, info);
	if (retval) {
		if (debug_level >= DEBUG_LEVEL_INFO)
			printk("%s(%d):%s block_til_ready() returned %d\n",
				 __FILE__,__LINE__, info->device_name, retval);
		goto cleanup;
	}

	if (debug_level >= DEBUG_LEVEL_INFO)
		printk("%s(%d):%s open() success\n",
			 __FILE__,__LINE__, info->device_name);
	retval = 0;

cleanup:
	if (retval) {
		if (tty->count == 1)
			info->port.tty = NULL; /* tty layer will release tty struct */
		if(info->port.count)
			info->port.count--;
	}

	return retval;
}

/* Called when port is closed. Wait for remaining data to be
 * sent. Disable port and free resources.
 */
static void close(struct tty_struct *tty, struct file *filp)
{
	SLMP_INFO * info = tty->driver_data;

	if (sanity_check(info, tty->name, "close"))
		return;

	if (debug_level >= DEBUG_LEVEL_INFO)
		printk("%s(%d):%s close() entry, count=%d\n",
			 __FILE__,__LINE__, info->device_name, info->port.count);

	if (tty_port_close_start(&info->port, tty, filp) == 0)
		goto cleanup;

	mutex_lock(&info->port.mutex);
 	if (info->port.flags & ASYNC_INITIALIZED)
 		wait_until_sent(tty, info->timeout);

	flush_buffer(tty);
	tty_ldisc_flush(tty);
	shutdown(info);
	mutex_unlock(&info->port.mutex);

	tty_port_close_end(&info->port, tty);
	info->port.tty = NULL;
cleanup:
	if (debug_level >= DEBUG_LEVEL_INFO)
		printk("%s(%d):%s close() exit, count=%d\n", __FILE__,__LINE__,
			tty->driver->name, info->port.count);
}

/* Called by tty_hangup() when a hangup is signaled.
 * This is the same as closing all open descriptors for the port.
 */
static void hangup(struct tty_struct *tty)
{
	SLMP_INFO *info = tty->driver_data;
	unsigned long flags;

	if (debug_level >= DEBUG_LEVEL_INFO)
		printk("%s(%d):%s hangup()\n",
			 __FILE__,__LINE__, info->device_name );

	if (sanity_check(info, tty->name, "hangup"))
		return;

	mutex_lock(&info->port.mutex);
	flush_buffer(tty);
	shutdown(info);

	spin_lock_irqsave(&info->port.lock, flags);
	info->port.count = 0;
	info->port.flags &= ~ASYNC_NORMAL_ACTIVE;
	info->port.tty = NULL;
	spin_unlock_irqrestore(&info->port.lock, flags);
	mutex_unlock(&info->port.mutex);

	wake_up_interruptible(&info->port.open_wait);
}

/* Set new termios settings
 */
static void set_termios(struct tty_struct *tty, struct ktermios *old_termios)
{
	SLMP_INFO *info = tty->driver_data;
	unsigned long flags;

	if (debug_level >= DEBUG_LEVEL_INFO)
		printk("%s(%d):%s set_termios()\n", __FILE__,__LINE__,
			tty->driver->name );

	change_params(info);

	/* Handle transition to B0 status */
	if (old_termios->c_cflag & CBAUD &&
	    !(tty->termios.c_cflag & CBAUD)) {
		info->serial_signals &= ~(SerialSignal_RTS + SerialSignal_DTR);
		spin_lock_irqsave(&info->lock,flags);
	 	set_signals(info);
		spin_unlock_irqrestore(&info->lock,flags);
	}

	/* Handle transition away from B0 status */
	if (!(old_termios->c_cflag & CBAUD) &&
	    tty->termios.c_cflag & CBAUD) {
		info->serial_signals |= SerialSignal_DTR;
 		if (!(tty->termios.c_cflag & CRTSCTS) ||
 		    !test_bit(TTY_THROTTLED, &tty->flags)) {
			info->serial_signals |= SerialSignal_RTS;
 		}
		spin_lock_irqsave(&info->lock,flags);
	 	set_signals(info);
		spin_unlock_irqrestore(&info->lock,flags);
	}

	/* Handle turning off CRTSCTS */
	if (old_termios->c_cflag & CRTSCTS &&
	    !(tty->termios.c_cflag & CRTSCTS)) {
		tty->hw_stopped = 0;
		tx_release(tty);
	}
}

/* Send a block of data
 *
 * Arguments:
 *
 * 	tty		pointer to tty information structure
 * 	buf		pointer to buffer containing send data
 * 	count		size of send data in bytes
 *
 * Return Value:	number of characters written
 */
static int write(struct tty_struct *tty,
		 const unsigned char *buf, int count)
{
	int	c, ret = 0;
	SLMP_INFO *info = tty->driver_data;
	unsigned long flags;

	if (debug_level >= DEBUG_LEVEL_INFO)
		printk("%s(%d):%s write() count=%d\n",
		       __FILE__,__LINE__,info->device_name,count);

	if (sanity_check(info, tty->name, "write"))
		goto cleanup;

	if (!info->tx_buf)
		goto cleanup;

	if (info->params.mode == MGSL_MODE_HDLC) {
		if (count > info->max_frame_size) {
			ret = -EIO;
			goto cleanup;
		}
		if (info->tx_active)
			goto cleanup;
		if (info->tx_count) {
			/* send accumulated data from send_char() calls */
			/* as frame and wait before accepting more data. */
			tx_load_dma_buffer(info, info->tx_buf, info->tx_count);
			goto start;
		}
		ret = info->tx_count = count;
		tx_load_dma_buffer(info, buf, count);
		goto start;
	}

	for (;;) {
		c = min_t(int, count,
			min(info->max_frame_size - info->tx_count - 1,
			    info->max_frame_size - info->tx_put));
		if (c <= 0)
			break;
			
		memcpy(info->tx_buf + info->tx_put, buf, c);

		spin_lock_irqsave(&info->lock,flags);
		info->tx_put += c;
		if (info->tx_put >= info->max_frame_size)
			info->tx_put -= info->max_frame_size;
		info->tx_count += c;
		spin_unlock_irqrestore(&info->lock,flags);

		buf += c;
		count -= c;
		ret += c;
	}

	if (info->params.mode == MGSL_MODE_HDLC) {
		if (count) {
			ret = info->tx_count = 0;
			goto cleanup;
		}
		tx_load_dma_buffer(info, info->tx_buf, info->tx_count);
	}
start:
 	if (info->tx_count && !tty->stopped && !tty->hw_stopped) {
		spin_lock_irqsave(&info->lock,flags);
		if (!info->tx_active)
		 	tx_start(info);
		spin_unlock_irqrestore(&info->lock,flags);
 	}

cleanup:
	if (debug_level >= DEBUG_LEVEL_INFO)
		printk( "%s(%d):%s write() returning=%d\n",
			__FILE__,__LINE__,info->device_name,ret);
	return ret;
}

/* Add a character to the transmit buffer.
 */
static int put_char(struct tty_struct *tty, unsigned char ch)
{
	SLMP_INFO *info = tty->driver_data;
	unsigned long flags;
	int ret = 0;

	if ( debug_level >= DEBUG_LEVEL_INFO ) {
		printk( "%s(%d):%s put_char(%d)\n",
			__FILE__,__LINE__,info->device_name,ch);
	}

	if (sanity_check(info, tty->name, "put_char"))
		return 0;

	if (!info->tx_buf)
		return 0;

	spin_lock_irqsave(&info->lock,flags);

	if ( (info->params.mode != MGSL_MODE_HDLC) ||
	     !info->tx_active ) {

		if (info->tx_count < info->max_frame_size - 1) {
			info->tx_buf[info->tx_put++] = ch;
			if (info->tx_put >= info->max_frame_size)
				info->tx_put -= info->max_frame_size;
			info->tx_count++;
			ret = 1;
		}
	}

	spin_unlock_irqrestore(&info->lock,flags);
	return ret;
}

/* Send a high-priority XON/XOFF character
 */
static void send_xchar(struct tty_struct *tty, char ch)
{
	SLMP_INFO *info = tty->driver_data;
	unsigned long flags;

	if (debug_level >= DEBUG_LEVEL_INFO)
		printk("%s(%d):%s send_xchar(%d)\n",
			 __FILE__,__LINE__, info->device_name, ch );

	if (sanity_check(info, tty->name, "send_xchar"))
		return;

	info->x_char = ch;
	if (ch) {
		/* Make sure transmit interrupts are on */
		spin_lock_irqsave(&info->lock,flags);
		if (!info->tx_enabled)
		 	tx_start(info);
		spin_unlock_irqrestore(&info->lock,flags);
	}
}

/* Wait until the transmitter is empty.
 */
static void wait_until_sent(struct tty_struct *tty, int timeout)
{
	SLMP_INFO * info = tty->driver_data;
	unsigned long orig_jiffies, char_time;

	if (!info )
		return;

	if (debug_level >= DEBUG_LEVEL_INFO)
		printk("%s(%d):%s wait_until_sent() entry\n",
			 __FILE__,__LINE__, info->device_name );

	if (sanity_check(info, tty->name, "wait_until_sent"))
		return;

	if (!test_bit(ASYNCB_INITIALIZED, &info->port.flags))
		goto exit;

	orig_jiffies = jiffies;

	/* Set check interval to 1/5 of estimated time to
	 * send a character, and make it at least 1. The check
	 * interval should also be less than the timeout.
	 * Note: use tight timings here to satisfy the NIST-PCTS.
	 */

	if ( info->params.data_rate ) {
	       	char_time = info->timeout/(32 * 5);
		if (!char_time)
			char_time++;
	} else
		char_time = 1;

	if (timeout)
		char_time = min_t(unsigned long, char_time, timeout);

	if ( info->params.mode == MGSL_MODE_HDLC ) {
		while (info->tx_active) {
			msleep_interruptible(jiffies_to_msecs(char_time));
			if (signal_pending(current))
				break;
			if (timeout && time_after(jiffies, orig_jiffies + timeout))
				break;
		}
	} else {
		/*
		 * TODO: determine if there is something similar to USC16C32
		 * 	 TXSTATUS_ALL_SENT status
		 */
		while ( info->tx_active && info->tx_enabled) {
			msleep_interruptible(jiffies_to_msecs(char_time));
			if (signal_pending(current))
				break;
			if (timeout && time_after(jiffies, orig_jiffies + timeout))
				break;
		}
	}

exit:
	if (debug_level >= DEBUG_LEVEL_INFO)
		printk("%s(%d):%s wait_until_sent() exit\n",
			 __FILE__,__LINE__, info->device_name );
}

/* Return the count of free bytes in transmit buffer
 */
static int write_room(struct tty_struct *tty)
{
	SLMP_INFO *info = tty->driver_data;
	int ret;

	if (sanity_check(info, tty->name, "write_room"))
		return 0;

	if (info->params.mode == MGSL_MODE_HDLC) {
		ret = (info->tx_active) ? 0 : HDLC_MAX_FRAME_SIZE;
	} else {
		ret = info->max_frame_size - info->tx_count - 1;
		if (ret < 0)
			ret = 0;
	}

	if (debug_level >= DEBUG_LEVEL_INFO)
		printk("%s(%d):%s write_room()=%d\n",
		       __FILE__, __LINE__, info->device_name, ret);

	return ret;
}

/* enable transmitter and send remaining buffered characters
 */
static void flush_chars(struct tty_struct *tty)
{
	SLMP_INFO *info = tty->driver_data;
	unsigned long flags;

	if ( debug_level >= DEBUG_LEVEL_INFO )
		printk( "%s(%d):%s flush_chars() entry tx_count=%d\n",
			__FILE__,__LINE__,info->device_name,info->tx_count);

	if (sanity_check(info, tty->name, "flush_chars"))
		return;

	if (info->tx_count <= 0 || tty->stopped || tty->hw_stopped ||
	    !info->tx_buf)
		return;

	if ( debug_level >= DEBUG_LEVEL_INFO )
		printk( "%s(%d):%s flush_chars() entry, starting transmitter\n",
			__FILE__,__LINE__,info->device_name );

	spin_lock_irqsave(&info->lock,flags);

	if (!info->tx_active) {
		if ( (info->params.mode == MGSL_MODE_HDLC) &&
			info->tx_count ) {
			/* operating in synchronous (frame oriented) mode */
			/* copy data from circular tx_buf to */
			/* transmit DMA buffer. */
			tx_load_dma_buffer(info,
				 info->tx_buf,info->tx_count);
		}
	 	tx_start(info);
	}

	spin_unlock_irqrestore(&info->lock,flags);
}

/* Discard all data in the send buffer
 */
static void flush_buffer(struct tty_struct *tty)
{
	SLMP_INFO *info = tty->driver_data;
	unsigned long flags;

	if (debug_level >= DEBUG_LEVEL_INFO)
		printk("%s(%d):%s flush_buffer() entry\n",
			 __FILE__,__LINE__, info->device_name );

	if (sanity_check(info, tty->name, "flush_buffer"))
		return;

	spin_lock_irqsave(&info->lock,flags);
	info->tx_count = info->tx_put = info->tx_get = 0;
	del_timer(&info->tx_timer);
	spin_unlock_irqrestore(&info->lock,flags);

	tty_wakeup(tty);
}

/* throttle (stop) transmitter
 */
static void tx_hold(struct tty_struct *tty)
{
	SLMP_INFO *info = tty->driver_data;
	unsigned long flags;

	if (sanity_check(info, tty->name, "tx_hold"))
		return;

	if ( debug_level >= DEBUG_LEVEL_INFO )
		printk("%s(%d):%s tx_hold()\n",
			__FILE__,__LINE__,info->device_name);

	spin_lock_irqsave(&info->lock,flags);
	if (info->tx_enabled)
	 	tx_stop(info);
	spin_unlock_irqrestore(&info->lock,flags);
}

/* release (start) transmitter
 */
static void tx_release(struct tty_struct *tty)
{
	SLMP_INFO *info = tty->driver_data;
	unsigned long flags;

	if (sanity_check(info, tty->name, "tx_release"))
		return;

	if ( debug_level >= DEBUG_LEVEL_INFO )
		printk("%s(%d):%s tx_release()\n",
			__FILE__,__LINE__,info->device_name);

	spin_lock_irqsave(&info->lock,flags);
	if (!info->tx_enabled)
	 	tx_start(info);
	spin_unlock_irqrestore(&info->lock,flags);
}

/* Service an IOCTL request
 *
 * Arguments:
 *
 * 	tty	pointer to tty instance data
 * 	cmd	IOCTL command code
 * 	arg	command argument/context
 *
 * Return Value:	0 if success, otherwise error code
 */
static int ioctl(struct tty_struct *tty,
		 unsigned int cmd, unsigned long arg)
{
	SLMP_INFO *info = tty->driver_data;
	void __user *argp = (void __user *)arg;

	if (debug_level >= DEBUG_LEVEL_INFO)
		printk("%s(%d):%s ioctl() cmd=%08X\n", __FILE__,__LINE__,
			info->device_name, cmd );

	if (sanity_check(info, tty->name, "ioctl"))
		return -ENODEV;

	if ((cmd != TIOCGSERIAL) && (cmd != TIOCSSERIAL) &&
	    (cmd != TIOCMIWAIT)) {
		if (tty->flags & (1 << TTY_IO_ERROR))
		    return -EIO;
	}

	switch (cmd) {
	case MGSL_IOCGPARAMS:
		return get_params(info, argp);
	case MGSL_IOCSPARAMS:
		return set_params(info, argp);
	case MGSL_IOCGTXIDLE:
		return get_txidle(info, argp);
	case MGSL_IOCSTXIDLE:
		return set_txidle(info, (int)arg);
	case MGSL_IOCTXENABLE:
		return tx_enable(info, (int)arg);
	case MGSL_IOCRXENABLE:
		return rx_enable(info, (int)arg);
	case MGSL_IOCTXABORT:
		return tx_abort(info);
	case MGSL_IOCGSTATS:
		return get_stats(info, argp);
	case MGSL_IOCWAITEVENT:
		return wait_mgsl_event(info, argp);
	case MGSL_IOCLOOPTXDONE:
		return 0; // TODO: Not supported, need to document
		/* Wait for modem input (DCD,RI,DSR,CTS) change
		 * as specified by mask in arg (TIOCM_RNG/DSR/CD/CTS)
		 */
	case TIOCMIWAIT:
		return modem_input_wait(info,(int)arg);
		
		/*
		 * Get counter of input serial line interrupts (DCD,RI,DSR,CTS)
		 * Return: write counters to the user passed counter struct
		 * NB: both 1->0 and 0->1 transitions are counted except for
		 *     RI where only 0->1 is counted.
		 */
	default:
		return -ENOIOCTLCMD;
	}
	return 0;
}

static int get_icount(struct tty_struct *tty,
				struct serial_icounter_struct *icount)
{
	SLMP_INFO *info = tty->driver_data;
	struct mgsl_icount cnow;	/* kernel counter temps */
	unsigned long flags;

	spin_lock_irqsave(&info->lock,flags);
	cnow = info->icount;
	spin_unlock_irqrestore(&info->lock,flags);

	icount->cts = cnow.cts;
	icount->dsr = cnow.dsr;
	icount->rng = cnow.rng;
	icount->dcd = cnow.dcd;
	icount->rx = cnow.rx;
	icount->tx = cnow.tx;
	icount->frame = cnow.frame;
	icount->overrun = cnow.overrun;
	icount->parity = cnow.parity;
	icount->brk = cnow.brk;
	icount->buf_overrun = cnow.buf_overrun;

	return 0;
}

/*
 * /proc fs routines....
 */

static inline void line_info(struct seq_file *m, SLMP_INFO *info)
{
	char	stat_buf[30];
	unsigned long flags;

	seq_printf(m, "%s: SCABase=%08x Mem=%08X StatusControl=%08x LCR=%08X\n"
		       "\tIRQ=%d MaxFrameSize=%u\n",
		info->device_name,
		info->phys_sca_base,
		info->phys_memory_base,
		info->phys_statctrl_base,
		info->phys_lcr_base,
		info->irq_level,
		info->max_frame_size );

	/* output current serial signal states */
	spin_lock_irqsave(&info->lock,flags);
 	get_signals(info);
	spin_unlock_irqrestore(&info->lock,flags);

	stat_buf[0] = 0;
	stat_buf[1] = 0;
	if (info->serial_signals & SerialSignal_RTS)
		strcat(stat_buf, "|RTS");
	if (info->serial_signals & SerialSignal_CTS)
		strcat(stat_buf, "|CTS");
	if (info->serial_signals & SerialSignal_DTR)
		strcat(stat_buf, "|DTR");
	if (info->serial_signals & SerialSignal_DSR)
		strcat(stat_buf, "|DSR");
	if (info->serial_signals & SerialSignal_DCD)
		strcat(stat_buf, "|CD");
	if (info->serial_signals & SerialSignal_RI)
		strcat(stat_buf, "|RI");

	if (info->params.mode == MGSL_MODE_HDLC) {
		seq_printf(m, "\tHDLC txok:%d rxok:%d",
			      info->icount.txok, info->icount.rxok);
		if (info->icount.txunder)
			seq_printf(m, " txunder:%d", info->icount.txunder);
		if (info->icount.txabort)
			seq_printf(m, " txabort:%d", info->icount.txabort);
		if (info->icount.rxshort)
			seq_printf(m, " rxshort:%d", info->icount.rxshort);
		if (info->icount.rxlong)
			seq_printf(m, " rxlong:%d", info->icount.rxlong);
		if (info->icount.rxover)
			seq_printf(m, " rxover:%d", info->icount.rxover);
		if (info->icount.rxcrc)
			seq_printf(m, " rxlong:%d", info->icount.rxcrc);
	} else {
		seq_printf(m, "\tASYNC tx:%d rx:%d",
			      info->icount.tx, info->icount.rx);
		if (info->icount.frame)
			seq_printf(m, " fe:%d", info->icount.frame);
		if (info->icount.parity)
			seq_printf(m, " pe:%d", info->icount.parity);
		if (info->icount.brk)
			seq_printf(m, " brk:%d", info->icount.brk);
		if (info->icount.overrun)
			seq_printf(m, " oe:%d", info->icount.overrun);
	}

	/* Append serial signal status to end */
	seq_printf(m, " %s\n", stat_buf+1);

	seq_printf(m, "\ttxactive=%d bh_req=%d bh_run=%d pending_bh=%x\n",
	 info->tx_active,info->bh_requested,info->bh_running,
	 info->pending_bh);
}

/* Called to print information about devices
 */
static int synclinkmp_proc_show(struct seq_file *m, void *v)
{
	SLMP_INFO *info;

	seq_printf(m, "synclinkmp driver:%s\n", driver_version);

	info = synclinkmp_device_list;
	while( info ) {
		line_info(m, info);
		info = info->next_device;
	}
	return 0;
}

static int synclinkmp_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, synclinkmp_proc_show, NULL);
}

static const struct file_operations synclinkmp_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= synclinkmp_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

/* Return the count of bytes in transmit buffer
 */
static int chars_in_buffer(struct tty_struct *tty)
{
	SLMP_INFO *info = tty->driver_data;

	if (sanity_check(info, tty->name, "chars_in_buffer"))
		return 0;

	if (debug_level >= DEBUG_LEVEL_INFO)
		printk("%s(%d):%s chars_in_buffer()=%d\n",
		       __FILE__, __LINE__, info->device_name, info->tx_count);

	return info->tx_count;
}

/* Signal remote device to throttle send data (our receive data)
 */
static void throttle(struct tty_struct * tty)
{
	SLMP_INFO *info = tty->driver_data;
	unsigned long flags;

	if (debug_level >= DEBUG_LEVEL_INFO)
		printk("%s(%d):%s throttle() entry\n",
			 __FILE__,__LINE__, info->device_name );

	if (sanity_check(info, tty->name, "throttle"))
		return;

	if (I_IXOFF(tty))
		send_xchar(tty, STOP_CHAR(tty));

 	if (tty->termios.c_cflag & CRTSCTS) {
		spin_lock_irqsave(&info->lock,flags);
		info->serial_signals &= ~SerialSignal_RTS;
	 	set_signals(info);
		spin_unlock_irqrestore(&info->lock,flags);
	}
}

/* Signal remote device to stop throttling send data (our receive data)
 */
static void unthrottle(struct tty_struct * tty)
{
	SLMP_INFO *info = tty->driver_data;
	unsigned long flags;

	if (debug_level >= DEBUG_LEVEL_INFO)
		printk("%s(%d):%s unthrottle() entry\n",
			 __FILE__,__LINE__, info->device_name );

	if (sanity_check(info, tty->name, "unthrottle"))
		return;

	if (I_IXOFF(tty)) {
		if (info->x_char)
			info->x_char = 0;
		else
			send_xchar(tty, START_CHAR(tty));
	}

 	if (tty->termios.c_cflag & CRTSCTS) {
		spin_lock_irqsave(&info->lock,flags);
		info->serial_signals |= SerialSignal_RTS;
	 	set_signals(info);
		spin_unlock_irqrestore(&info->lock,flags);
	}
}

/* set or clear transmit break condition
 * break_state	-1=set break condition, 0=clear
 */
static int set_break(struct tty_struct *tty, int break_state)
{
	unsigned char RegValue;
	SLMP_INFO * info = tty->driver_data;
	unsigned long flags;

	if (debug_level >= DEBUG_LEVEL_INFO)
		printk("%s(%d):%s set_break(%d)\n",
			 __FILE__,__LINE__, info->device_name, break_state);

	if (sanity_check(info, tty->name, "set_break"))
		return -EINVAL;

	spin_lock_irqsave(&info->lock,flags);
	RegValue = read_reg(info, CTL);
 	if (break_state == -1)
		RegValue |= BIT3;
	else
		RegValue &= ~BIT3;
	write_reg(info, CTL, RegValue);
	spin_unlock_irqrestore(&info->lock,flags);
	return 0;
}

#if SYNCLINK_GENERIC_HDLC

/**
 * called by generic HDLC layer when protocol selected (PPP, frame relay, etc.)
 * set encoding and frame check sequence (FCS) options
 *
 * dev       pointer to network device structure
 * encoding  serial encoding setting
 * parity    FCS setting
 *
 * returns 0 if success, otherwise error code
 */
static int hdlcdev_attach(struct net_device *dev, unsigned short encoding,
			  unsigned short parity)
{
	SLMP_INFO *info = dev_to_port(dev);
	unsigned char  new_encoding;
	unsigned short new_crctype;

	/* return error if TTY interface open */
	if (info->port.count)
		return -EBUSY;

	switch (encoding)
	{
	case ENCODING_NRZ:        new_encoding = HDLC_ENCODING_NRZ; break;
	case ENCODING_NRZI:       new_encoding = HDLC_ENCODING_NRZI_SPACE; break;
	case ENCODING_FM_MARK:    new_encoding = HDLC_ENCODING_BIPHASE_MARK; break;
	case ENCODING_FM_SPACE:   new_encoding = HDLC_ENCODING_BIPHASE_SPACE; break;
	case ENCODING_MANCHESTER: new_encoding = HDLC_ENCODING_BIPHASE_LEVEL; break;
	default: return -EINVAL;
	}

	switch (parity)
	{
	case PARITY_NONE:            new_crctype = HDLC_CRC_NONE; break;
	case PARITY_CRC16_PR1_CCITT: new_crctype = HDLC_CRC_16_CCITT; break;
	case PARITY_CRC32_PR1_CCITT: new_crctype = HDLC_CRC_32_CCITT; break;
	default: return -EINVAL;
	}

	info->params.encoding = new_encoding;
	info->params.crc_type = new_crctype;

	/* if network interface up, reprogram hardware */
	if (info->netcount)
		program_hw(info);

	return 0;
}

/**
 * called by generic HDLC layer to send frame
 *
 * skb  socket buffer containing HDLC frame
 * dev  pointer to network device structure
 */
static netdev_tx_t hdlcdev_xmit(struct sk_buff *skb,
				      struct net_device *dev)
{
	SLMP_INFO *info = dev_to_port(dev);
	unsigned long flags;

	if (debug_level >= DEBUG_LEVEL_INFO)
		printk(KERN_INFO "%s:hdlc_xmit(%s)\n",__FILE__,dev->name);

	/* stop sending until this frame completes */
	netif_stop_queue(dev);

	/* copy data to device buffers */
	info->tx_count = skb->len;
	tx_load_dma_buffer(info, skb->data, skb->len);

	/* update network statistics */
	dev->stats.tx_packets++;
	dev->stats.tx_bytes += skb->len;

	/* done with socket buffer, so free it */
	dev_kfree_skb(skb);

	/* save start time for transmit timeout detection */
	dev->trans_start = jiffies;

	/* start hardware transmitter if necessary */
	spin_lock_irqsave(&info->lock,flags);
	if (!info->tx_active)
	 	tx_start(info);
	spin_unlock_irqrestore(&info->lock,flags);

	return NETDEV_TX_OK;
}

/**
 * called by network layer when interface enabled
 * claim resources and initialize hardware
 *
 * dev  pointer to network device structure
 *
 * returns 0 if success, otherwise error code
 */
static int hdlcdev_open(struct net_device *dev)
{
	SLMP_INFO *info = dev_to_port(dev);
	int rc;
	unsigned long flags;

	if (debug_level >= DEBUG_LEVEL_INFO)
		printk("%s:hdlcdev_open(%s)\n",__FILE__,dev->name);

	/* generic HDLC layer open processing */
	if ((rc = hdlc_open(dev)))
		return rc;

	/* arbitrate between network and tty opens */
	spin_lock_irqsave(&info->netlock, flags);
	if (info->port.count != 0 || info->netcount != 0) {
		printk(KERN_WARNING "%s: hdlc_open returning busy\n", dev->name);
		spin_unlock_irqrestore(&info->netlock, flags);
		return -EBUSY;
	}
	info->netcount=1;
	spin_unlock_irqrestore(&info->netlock, flags);

	/* claim resources and init adapter */
	if ((rc = startup(info)) != 0) {
		spin_lock_irqsave(&info->netlock, flags);
		info->netcount=0;
		spin_unlock_irqrestore(&info->netlock, flags);
		return rc;
	}

	/* assert DTR and RTS, apply hardware settings */
	info->serial_signals |= SerialSignal_RTS + SerialSignal_DTR;
	program_hw(info);

	/* enable network layer transmit */
	dev->trans_start = jiffies;
	netif_start_queue(dev);

	/* inform generic HDLC layer of current DCD status */
	spin_lock_irqsave(&info->lock, flags);
	get_signals(info);
	spin_unlock_irqrestore(&info->lock, flags);
	if (info->serial_signals & SerialSignal_DCD)
		netif_carrier_on(dev);
	else
		netif_carrier_off(dev);
	return 0;
}

/**
 * called by network layer when interface is disabled
 * shutdown hardware and release resources
 *
 * dev  pointer to network device structure
 *
 * returns 0 if success, otherwise error code
 */
static int hdlcdev_close(struct net_device *dev)
{
	SLMP_INFO *info = dev_to_port(dev);
	unsigned long flags;

	if (debug_level >= DEBUG_LEVEL_INFO)
		printk("%s:hdlcdev_close(%s)\n",__FILE__,dev->name);

	netif_stop_queue(dev);

	/* shutdown adapter and release resources */
	shutdown(info);

	hdlc_close(dev);

	spin_lock_irqsave(&info->netlock, flags);
	info->netcount=0;
	spin_unlock_irqrestore(&info->netlock, flags);

	return 0;
}

/**
 * called by network layer to process IOCTL call to network device
 *
 * dev  pointer to network device structure
 * ifr  pointer to network interface request structure
 * cmd  IOCTL command code
 *
 * returns 0 if success, otherwise error code
 */
static int hdlcdev_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	const size_t size = sizeof(sync_serial_settings);
	sync_serial_settings new_line;
	sync_serial_settings __user *line = ifr->ifr_settings.ifs_ifsu.sync;
	SLMP_INFO *info = dev_to_port(dev);
	unsigned int flags;

	if (debug_level >= DEBUG_LEVEL_INFO)
		printk("%s:hdlcdev_ioctl(%s)\n",__FILE__,dev->name);

	/* return error if TTY interface open */
	if (info->port.count)
		return -EBUSY;

	if (cmd != SIOCWANDEV)
		return hdlc_ioctl(dev, ifr, cmd);

	switch(ifr->ifr_settings.type) {
	case IF_GET_IFACE: /* return current sync_serial_settings */

		ifr->ifr_settings.type = IF_IFACE_SYNC_SERIAL;
		if (ifr->ifr_settings.size < size) {
			ifr->ifr_settings.size = size; /* data size wanted */
			return -ENOBUFS;
		}

		flags = info->params.flags & (HDLC_FLAG_RXC_RXCPIN | HDLC_FLAG_RXC_DPLL |
					      HDLC_FLAG_RXC_BRG    | HDLC_FLAG_RXC_TXCPIN |
					      HDLC_FLAG_TXC_TXCPIN | HDLC_FLAG_TXC_DPLL |
					      HDLC_FLAG_TXC_BRG    | HDLC_FLAG_TXC_RXCPIN);

		switch (flags){
		case (HDLC_FLAG_RXC_RXCPIN | HDLC_FLAG_TXC_TXCPIN): new_line.clock_type = CLOCK_EXT; break;
		case (HDLC_FLAG_RXC_BRG    | HDLC_FLAG_TXC_BRG):    new_line.clock_type = CLOCK_INT; break;
		case (HDLC_FLAG_RXC_RXCPIN | HDLC_FLAG_TXC_BRG):    new_line.clock_type = CLOCK_TXINT; break;
		case (HDLC_FLAG_RXC_RXCPIN | HDLC_FLAG_TXC_RXCPIN): new_line.clock_type = CLOCK_TXFROMRX; break;
		default: new_line.clock_type = CLOCK_DEFAULT;
		}

		new_line.clock_rate = info->params.clock_speed;
		new_line.loopback   = info->params.loopback ? 1:0;

		if (copy_to_user(line, &new_line, size))
			return -EFAULT;
		return 0;

	case IF_IFACE_SYNC_SERIAL: /* set sync_serial_settings */

		if(!capable(CAP_NET_ADMIN))
			return -EPERM;
		if (copy_from_user(&new_line, line, size))
			return -EFAULT;

		switch (new_line.clock_type)
		{
		case CLOCK_EXT:      flags = HDLC_FLAG_RXC_RXCPIN | HDLC_FLAG_TXC_TXCPIN; break;
		case CLOCK_TXFROMRX: flags = HDLC_FLAG_RXC_RXCPIN | HDLC_FLAG_TXC_RXCPIN; break;
		case CLOCK_INT:      flags = HDLC_FLAG_RXC_BRG    | HDLC_FLAG_TXC_BRG;    break;
		case CLOCK_TXINT:    flags = HDLC_FLAG_RXC_RXCPIN | HDLC_FLAG_TXC_BRG;    break;
		case CLOCK_DEFAULT:  flags = info->params.flags &
					     (HDLC_FLAG_RXC_RXCPIN | HDLC_FLAG_RXC_DPLL |
					      HDLC_FLAG_RXC_BRG    | HDLC_FLAG_RXC_TXCPIN |
					      HDLC_FLAG_TXC_TXCPIN | HDLC_FLAG_TXC_DPLL |
					      HDLC_FLAG_TXC_BRG    | HDLC_FLAG_TXC_RXCPIN); break;
		default: return -EINVAL;
		}

		if (new_line.loopback != 0 && new_line.loopback != 1)
			return -EINVAL;

		info->params.flags &= ~(HDLC_FLAG_RXC_RXCPIN | HDLC_FLAG_RXC_DPLL |
					HDLC_FLAG_RXC_BRG    | HDLC_FLAG_RXC_TXCPIN |
					HDLC_FLAG_TXC_TXCPIN | HDLC_FLAG_TXC_DPLL |
					HDLC_FLAG_TXC_BRG    | HDLC_FLAG_TXC_RXCPIN);
		info->params.flags |= flags;

		info->params.loopback = new_line.loopback;

		if (flags & (HDLC_FLAG_RXC_BRG | HDLC_FLAG_TXC_BRG))
			info->params.clock_speed = new_line.clock_rate;
		else
			info->params.clock_speed = 0;

		/* if network interface up, reprogram hardware */
		if (info->netcount)
			program_hw(info);
		return 0;

	default:
		return hdlc_ioctl(dev, ifr, cmd);
	}
}

/**
 * called by network layer when transmit timeout is detected
 *
 * dev  pointer to network device structure
 */
static void hdlcdev_tx_timeout(struct net_device *dev)
{
	SLMP_INFO *info = dev_to_port(dev);
	unsigned long flags;

	if (debug_level >= DEBUG_LEVEL_INFO)
		printk("hdlcdev_tx_timeout(%s)\n",dev->name);

	dev->stats.tx_errors++;
	dev->stats.tx_aborted_errors++;

	spin_lock_irqsave(&info->lock,flags);
	tx_stop(info);
	spin_unlock_irqrestore(&info->lock,flags);

	netif_wake_queue(dev);
}

/**
 * called by device driver when transmit completes
 * reenable network layer transmit if stopped
 *
 * info  pointer to device instance information
 */
static void hdlcdev_tx_done(SLMP_INFO *info)
{
	if (netif_queue_stopped(info->netdev))
		netif_wake_queue(info->netdev);
}

/**
 * called by device driver when frame received
 * pass frame to network layer
 *
 * info  pointer to device instance information
 * buf   pointer to buffer contianing frame data
 * size  count of data bytes in buf
 */
static void hdlcdev_rx(SLMP_INFO *info, char *buf, int size)
{
	struct sk_buff *skb = dev_alloc_skb(size);
	struct net_device *dev = info->netdev;

	if (debug_level >= DEBUG_LEVEL_INFO)
		printk("hdlcdev_rx(%s)\n",dev->name);

	if (skb == NULL) {
		printk(KERN_NOTICE "%s: can't alloc skb, dropping packet\n",
		       dev->name);
		dev->stats.rx_dropped++;
		return;
	}

	memcpy(skb_put(skb, size), buf, size);

	skb->protocol = hdlc_type_trans(skb, dev);

	dev->stats.rx_packets++;
	dev->stats.rx_bytes += size;

	netif_rx(skb);
}

static const struct net_device_ops hdlcdev_ops = {
	.ndo_open       = hdlcdev_open,
	.ndo_stop       = hdlcdev_close,
	.ndo_change_mtu = hdlc_change_mtu,
	.ndo_start_xmit = hdlc_start_xmit,
	.ndo_do_ioctl   = hdlcdev_ioctl,
	.ndo_tx_timeout = hdlcdev_tx_timeout,
};

/**
 * called by device driver when adding device instance
 * do generic HDLC initialization
 *
 * info  pointer to device instance information
 *
 * returns 0 if success, otherwise error code
 */
static int hdlcdev_init(SLMP_INFO *info)
{
	int rc;
	struct net_device *dev;
	hdlc_device *hdlc;

	/* allocate and initialize network and HDLC layer objects */

	if (!(dev = alloc_hdlcdev(info))) {
		printk(KERN_ERR "%s:hdlc device allocation failure\n",__FILE__);
		return -ENOMEM;
	}

	/* for network layer reporting purposes only */
	dev->mem_start = info->phys_sca_base;
	dev->mem_end   = info->phys_sca_base + SCA_BASE_SIZE - 1;
	dev->irq       = info->irq_level;

	/* network layer callbacks and settings */
	dev->netdev_ops	    = &hdlcdev_ops;
	dev->watchdog_timeo = 10 * HZ;
	dev->tx_queue_len   = 50;

	/* generic HDLC layer callbacks and settings */
	hdlc         = dev_to_hdlc(dev);
	hdlc->attach = hdlcdev_attach;
	hdlc->xmit   = hdlcdev_xmit;

	/* register objects with HDLC layer */
	if ((rc = register_hdlc_device(dev))) {
		printk(KERN_WARNING "%s:unable to register hdlc device\n",__FILE__);
		free_netdev(dev);
		return rc;
	}

	info->netdev = dev;
	return 0;
}

/**
 * called by device driver when removing device instance
 * do generic HDLC cleanup
 *
 * info  pointer to device instance information
 */
static void hdlcdev_exit(SLMP_INFO *info)
{
	unregister_hdlc_device(info->netdev);
	free_netdev(info->netdev);
	info->netdev = NULL;
}

#endif /* CONFIG_HDLC */


/* Return next bottom half action to perform.
 * Return Value:	BH action code or 0 if nothing to do.
 */
static int bh_action(SLMP_INFO *info)
{
	unsigned long flags;
	int rc = 0;

	spin_lock_irqsave(&info->lock,flags);

	if (info->pending_bh & BH_RECEIVE) {
		info->pending_bh &= ~BH_RECEIVE;
		rc = BH_RECEIVE;
	} else if (info->pending_bh & BH_TRANSMIT) {
		info->pending_bh &= ~BH_TRANSMIT;
		rc = BH_TRANSMIT;
	} else if (info->pending_bh & BH_STATUS) {
		info->pending_bh &= ~BH_STATUS;
		rc = BH_STATUS;
	}

	if (!rc) {
		/* Mark BH routine as complete */
		info->bh_running = false;
		info->bh_requested = false;
	}

	spin_unlock_irqrestore(&info->lock,flags);

	return rc;
}

/* Perform bottom half processing of work items queued by ISR.
 */
static void bh_handler(struct work_struct *work)
{
	SLMP_INFO *info = container_of(work, SLMP_INFO, task);
	int action;

	if (!info)
		return;

	if ( debug_level >= DEBUG_LEVEL_BH )
		printk( "%s(%d):%s bh_handler() entry\n",
			__FILE__,__LINE__,info->device_name);

	info->bh_running = true;

	while((action = bh_action(info)) != 0) {

		/* Process work item */
		if ( debug_level >= DEBUG_LEVEL_BH )
			printk( "%s(%d):%s bh_handler() work item action=%d\n",
				__FILE__,__LINE__,info->device_name, action);

		switch (action) {

		case BH_RECEIVE:
			bh_receive(info);
			break;
		case BH_TRANSMIT:
			bh_transmit(info);
			break;
		case BH_STATUS:
			bh_status(info);
			break;
		default:
			/* unknown work item ID */
			printk("%s(%d):%s Unknown work item ID=%08X!\n",
				__FILE__,__LINE__,info->device_name,action);
			break;
		}
	}

	if ( debug_level >= DEBUG_LEVEL_BH )
		printk( "%s(%d):%s bh_handler() exit\n",
			__FILE__,__LINE__,info->device_name);
}

static void bh_receive(SLMP_INFO *info)
{
	if ( debug_level >= DEBUG_LEVEL_BH )
		printk( "%s(%d):%s bh_receive()\n",
			__FILE__,__LINE__,info->device_name);

	while( rx_get_frame(info) );
}

static void bh_transmit(SLMP_INFO *info)
{
	struct tty_struct *tty = info->port.tty;

	if ( debug_level >= DEBUG_LEVEL_BH )
		printk( "%s(%d):%s bh_transmit() entry\n",
			__FILE__,__LINE__,info->device_name);

	if (tty)
		tty_wakeup(tty);
}

static void bh_status(SLMP_INFO *info)
{
	if ( debug_level >= DEBUG_LEVEL_BH )
		printk( "%s(%d):%s bh_status() entry\n",
			__FILE__,__LINE__,info->device_name);

	info->ri_chkcount = 0;
	info->dsr_chkcount = 0;
	info->dcd_chkcount = 0;
	info->cts_chkcount = 0;
}

static void isr_timer(SLMP_INFO * info)
{
	unsigned char timer = (info->port_num & 1) ? TIMER2 : TIMER0;

	/* IER2<7..4> = timer<3..0> interrupt enables (0=disabled) */
	write_reg(info, IER2, 0);

	/* TMCS, Timer Control/Status Register
	 *
	 * 07      CMF, Compare match flag (read only) 1=match
	 * 06      ECMI, CMF Interrupt Enable: 0=disabled
	 * 05      Reserved, must be 0
	 * 04      TME, Timer Enable
	 * 03..00  Reserved, must be 0
	 *
	 * 0000 0000
	 */
	write_reg(info, (unsigned char)(timer + TMCS), 0);

	info->irq_occurred = true;

	if ( debug_level >= DEBUG_LEVEL_ISR )
		printk("%s(%d):%s isr_timer()\n",
			__FILE__,__LINE__,info->device_name);
}

static void isr_rxint(SLMP_INFO * info)
{
 	struct tty_struct *tty = info->port.tty;
 	struct	mgsl_icount *icount = &info->icount;
	unsigned char status = read_reg(info, SR1) & info->ie1_value & (FLGD + IDLD + CDCD + BRKD);
	unsigned char status2 = read_reg(info, SR2) & info->ie2_value & OVRN;

	/* clear status bits */
	if (status)
		write_reg(info, SR1, status);

	if (status2)
		write_reg(info, SR2, status2);
	
	if ( debug_level >= DEBUG_LEVEL_ISR )
		printk("%s(%d):%s isr_rxint status=%02X %02x\n",
			__FILE__,__LINE__,info->device_name,status,status2);

	if (info->params.mode == MGSL_MODE_ASYNC) {
		if (status & BRKD) {
			icount->brk++;

			/* process break detection if tty control
			 * is not set to ignore it
			 */
			if ( tty ) {
				if (!(status & info->ignore_status_mask1)) {
					if (info->read_status_mask1 & BRKD) {
						tty_insert_flip_char(tty, 0, TTY_BREAK);
						if (info->port.flags & ASYNC_SAK)
							do_SAK(tty);
					}
				}
			}
		}
	}
	else {
		if (status & (FLGD|IDLD)) {
			if (status & FLGD)
				info->icount.exithunt++;
			else if (status & IDLD)
				info->icount.rxidle++;
			wake_up_interruptible(&info->event_wait_q);
		}
	}

	if (status & CDCD) {
		/* simulate a common modem status change interrupt
		 * for our handler
		 */
		get_signals( info );
		isr_io_pin(info,
			MISCSTATUS_DCD_LATCHED|(info->serial_signals&SerialSignal_DCD));
	}
}

/*
 * handle async rx data interrupts
 */
static void isr_rxrdy(SLMP_INFO * info)
{
	u16 status;
	unsigned char DataByte;
 	struct tty_struct *tty = info->port.tty;
 	struct	mgsl_icount *icount = &info->icount;

	if ( debug_level >= DEBUG_LEVEL_ISR )
		printk("%s(%d):%s isr_rxrdy\n",
			__FILE__,__LINE__,info->device_name);

	while((status = read_reg(info,CST0)) & BIT0)
	{
		int flag = 0;
		bool over = false;
		DataByte = read_reg(info,TRB);

		icount->rx++;

		if ( status & (PE + FRME + OVRN) ) {
			printk("%s(%d):%s rxerr=%04X\n",
				__FILE__,__LINE__,info->device_name,status);

			/* update error statistics */
			if (status & PE)
				icount->parity++;
			else if (status & FRME)
				icount->frame++;
			else if (status & OVRN)
				icount->overrun++;

			/* discard char if tty control flags say so */
			if (status & info->ignore_status_mask2)
				continue;

			status &= info->read_status_mask2;

			if ( tty ) {
				if (status & PE)
					flag = TTY_PARITY;
				else if (status & FRME)
					flag = TTY_FRAME;
				if (status & OVRN) {
					/* Overrun is special, since it's
					 * reported immediately, and doesn't
					 * affect the current character
					 */
					over = true;
				}
			}
		}	/* end of if (error) */

		if ( tty ) {
			tty_insert_flip_char(tty, DataByte, flag);
			if (over)
				tty_insert_flip_char(tty, 0, TTY_OVERRUN);
		}
	}

	if ( debug_level >= DEBUG_LEVEL_ISR ) {
		printk("%s(%d):%s rx=%d brk=%d parity=%d frame=%d overrun=%d\n",
			__FILE__,__LINE__,info->device_name,
			icount->rx,icount->brk,icount->parity,
			icount->frame,icount->overrun);
	}

	if ( tty )
		tty_flip_buffer_push(tty);
}

static void isr_txeom(SLMP_INFO * info, unsigned char status)
{
	if ( debug_level >= DEBUG_LEVEL_ISR )
		printk("%s(%d):%s isr_txeom status=%02x\n",
			__FILE__,__LINE__,info->device_name,status);

	write_reg(info, TXDMA + DIR, 0x00); /* disable Tx DMA IRQs */
	write_reg(info, TXDMA + DSR, 0xc0); /* clear IRQs and disable DMA */
	write_reg(info, TXDMA + DCMD, SWABORT);	/* reset/init DMA channel */

	if (status & UDRN) {
		write_reg(info, CMD, TXRESET);
		write_reg(info, CMD, TXENABLE);
	} else
		write_reg(info, CMD, TXBUFCLR);

	/* disable and clear tx interrupts */
	info->ie0_value &= ~TXRDYE;
	info->ie1_value &= ~(IDLE + UDRN);
	write_reg16(info, IE0, (unsigned short)((info->ie1_value << 8) + info->ie0_value));
	write_reg(info, SR1, (unsigned char)(UDRN + IDLE));

	if ( info->tx_active ) {
		if (info->params.mode != MGSL_MODE_ASYNC) {
			if (status & UDRN)
				info->icount.txunder++;
			else if (status & IDLE)
				info->icount.txok++;
		}

		info->tx_active = false;
		info->tx_count = info->tx_put = info->tx_get = 0;

		del_timer(&info->tx_timer);

		if (info->params.mode != MGSL_MODE_ASYNC && info->drop_rts_on_tx_done ) {
			info->serial_signals &= ~SerialSignal_RTS;
			info->drop_rts_on_tx_done = false;
			set_signals(info);
		}

#if SYNCLINK_GENERIC_HDLC
		if (info->netcount)
			hdlcdev_tx_done(info);
		else
#endif
		{
			if (info->port.tty && (info->port.tty->stopped || info->port.tty->hw_stopped)) {
				tx_stop(info);
				return;
			}
			info->pending_bh |= BH_TRANSMIT;
		}
	}
}


/*
 * handle tx status interrupts
 */
static void isr_txint(SLMP_INFO * info)
{
	unsigned char status = read_reg(info, SR1) & info->ie1_value & (UDRN + IDLE + CCTS);

	/* clear status bits */
	write_reg(info, SR1, status);

	if ( debug_level >= DEBUG_LEVEL_ISR )
		printk("%s(%d):%s isr_txint status=%02x\n",
			__FILE__,__LINE__,info->device_name,status);

	if (status & (UDRN + IDLE))
		isr_txeom(info, status);

	if (status & CCTS) {
		/* simulate a common modem status change interrupt
		 * for our handler
		 */
		get_signals( info );
		isr_io_pin(info,
			MISCSTATUS_CTS_LATCHED|(info->serial_signals&SerialSignal_CTS));

	}
}

/*
 * handle async tx data interrupts
 */
static void isr_txrdy(SLMP_INFO * info)
{
	if ( debug_level >= DEBUG_LEVEL_ISR )
		printk("%s(%d):%s isr_txrdy() tx_count=%d\n",
			__FILE__,__LINE__,info->device_name,info->tx_count);

	if (info->params.mode != MGSL_MODE_ASYNC) {
		/* disable TXRDY IRQ, enable IDLE IRQ */
		info->ie0_value &= ~TXRDYE;
		info->ie1_value |= IDLE;
		write_reg16(info, IE0, (unsigned short)((info->ie1_value << 8) + info->ie0_value));
		return;
	}

	if (info->port.tty && (info->port.tty->stopped || info->port.tty->hw_stopped)) {
		tx_stop(info);
		return;
	}

	if ( info->tx_count )
		tx_load_fifo( info );
	else {
		info->tx_active = false;
		info->ie0_value &= ~TXRDYE;
		write_reg(info, IE0, info->ie0_value);
	}

	if (info->tx_count < WAKEUP_CHARS)
		info->pending_bh |= BH_TRANSMIT;
}

static void isr_rxdmaok(SLMP_INFO * info)
{
	/* BIT7 = EOT (end of transfer)
	 * BIT6 = EOM (end of message/frame)
	 */
	unsigned char status = read_reg(info,RXDMA + DSR) & 0xc0;

	/* clear IRQ (BIT0 must be 1 to prevent clearing DE bit) */
	write_reg(info, RXDMA + DSR, (unsigned char)(status | 1));

	if ( debug_level >= DEBUG_LEVEL_ISR )
		printk("%s(%d):%s isr_rxdmaok(), status=%02x\n",
			__FILE__,__LINE__,info->device_name,status);

	info->pending_bh |= BH_RECEIVE;
}

static void isr_rxdmaerror(SLMP_INFO * info)
{
	/* BIT5 = BOF (buffer overflow)
	 * BIT4 = COF (counter overflow)
	 */
	unsigned char status = read_reg(info,RXDMA + DSR) & 0x30;

	/* clear IRQ (BIT0 must be 1 to prevent clearing DE bit) */
	write_reg(info, RXDMA + DSR, (unsigned char)(status | 1));

	if ( debug_level >= DEBUG_LEVEL_ISR )
		printk("%s(%d):%s isr_rxdmaerror(), status=%02x\n",
			__FILE__,__LINE__,info->device_name,status);

	info->rx_overflow = true;
	info->pending_bh |= BH_RECEIVE;
}

static void isr_txdmaok(SLMP_INFO * info)
{
	unsigned char status_reg1 = read_reg(info, SR1);

	write_reg(info, TXDMA + DIR, 0x00);	/* disable Tx DMA IRQs */
	write_reg(info, TXDMA + DSR, 0xc0); /* clear IRQs and disable DMA */
	write_reg(info, TXDMA + DCMD, SWABORT);	/* reset/init DMA channel */

	if ( debug_level >= DEBUG_LEVEL_ISR )
		printk("%s(%d):%s isr_txdmaok(), status=%02x\n",
			__FILE__,__LINE__,info->device_name,status_reg1);

	/* program TXRDY as FIFO empty flag, enable TXRDY IRQ */
	write_reg16(info, TRC0, 0);
	info->ie0_value |= TXRDYE;
	write_reg(info, IE0, info->ie0_value);
}

static void isr_txdmaerror(SLMP_INFO * info)
{
	/* BIT5 = BOF (buffer overflow)
	 * BIT4 = COF (counter overflow)
	 */
	unsigned char status = read_reg(info,TXDMA + DSR) & 0x30;

	/* clear IRQ (BIT0 must be 1 to prevent clearing DE bit) */
	write_reg(info, TXDMA + DSR, (unsigned char)(status | 1));

	if ( debug_level >= DEBUG_LEVEL_ISR )
		printk("%s(%d):%s isr_txdmaerror(), status=%02x\n",
			__FILE__,__LINE__,info->device_name,status);
}

/* handle input serial signal changes
 */
static void isr_io_pin( SLMP_INFO *info, u16 status )
{
 	struct	mgsl_icount *icount;

	if ( debug_level >= DEBUG_LEVEL_ISR )
		printk("%s(%d):isr_io_pin status=%04X\n",
			__FILE__,__LINE__,status);

	if (status & (MISCSTATUS_CTS_LATCHED | MISCSTATUS_DCD_LATCHED |
	              MISCSTATUS_DSR_LATCHED | MISCSTATUS_RI_LATCHED) ) {
		icount = &info->icount;
		/* update input line counters */
		if (status & MISCSTATUS_RI_LATCHED) {
			icount->rng++;
			if ( status & SerialSignal_RI )
				info->input_signal_events.ri_up++;
			else
				info->input_signal_events.ri_down++;
		}
		if (status & MISCSTATUS_DSR_LATCHED) {
			icount->dsr++;
			if ( status & SerialSignal_DSR )
				info->input_signal_events.dsr_up++;
			else
				info->input_signal_events.dsr_down++;
		}
		if (status & MISCSTATUS_DCD_LATCHED) {
			if ((info->dcd_chkcount)++ >= IO_PIN_SHUTDOWN_LIMIT) {
				info->ie1_value &= ~CDCD;
				write_reg(info, IE1, info->ie1_value);
			}
			icount->dcd++;
			if (status & SerialSignal_DCD) {
				info->input_signal_events.dcd_up++;
			} else
				info->input_signal_events.dcd_down++;
#if SYNCLINK_GENERIC_HDLC
			if (info->netcount) {
				if (status & SerialSignal_DCD)
					netif_carrier_on(info->netdev);
				else
					netif_carrier_off(info->netdev);
			}
#endif
		}
		if (status & MISCSTATUS_CTS_LATCHED)
		{
			if ((info->cts_chkcount)++ >= IO_PIN_SHUTDOWN_LIMIT) {
				info->ie1_value &= ~CCTS;
				write_reg(info, IE1, info->ie1_value);
			}
			icount->cts++;
			if ( status & SerialSignal_CTS )
				info->input_signal_events.cts_up++;
			else
				info->input_signal_events.cts_down++;
		}
		wake_up_interruptible(&info->status_event_wait_q);
		wake_up_interruptible(&info->event_wait_q);

		if ( (info->port.flags & ASYNC_CHECK_CD) &&
		     (status & MISCSTATUS_DCD_LATCHED) ) {
			if ( debug_level >= DEBUG_LEVEL_ISR )
				printk("%s CD now %s...", info->device_name,
				       (status & SerialSignal_DCD) ? "on" : "off");
			if (status & SerialSignal_DCD)
				wake_up_interruptible(&info->port.open_wait);
			else {
				if ( debug_level >= DEBUG_LEVEL_ISR )
					printk("doing serial hangup...");
				if (info->port.tty)
					tty_hangup(info->port.tty);
			}
		}

		if (tty_port_cts_enabled(&info->port) &&
		     (status & MISCSTATUS_CTS_LATCHED) ) {
			if ( info->port.tty ) {
				if (info->port.tty->hw_stopped) {
					if (status & SerialSignal_CTS) {
						if ( debug_level >= DEBUG_LEVEL_ISR )
							printk("CTS tx start...");
			 			info->port.tty->hw_stopped = 0;
						tx_start(info);
						info->pending_bh |= BH_TRANSMIT;
						return;
					}
				} else {
					if (!(status & SerialSignal_CTS)) {
						if ( debug_level >= DEBUG_LEVEL_ISR )
							printk("CTS tx stop...");
			 			info->port.tty->hw_stopped = 1;
						tx_stop(info);
					}
				}
			}
		}
	}

	info->pending_bh |= BH_STATUS;
}

/* Interrupt service routine entry point.
 *
 * Arguments:
 * 	irq		interrupt number that caused interrupt
 * 	dev_id		device ID supplied during interrupt registration
 * 	regs		interrupted processor context
 */
static irqreturn_t synclinkmp_interrupt(int dummy, void *dev_id)
{
	SLMP_INFO *info = dev_id;
	unsigned char status, status0, status1=0;
	unsigned char dmastatus, dmastatus0, dmastatus1=0;
	unsigned char timerstatus0, timerstatus1=0;
	unsigned char shift;
	unsigned int i;
	unsigned short tmp;

	if ( debug_level >= DEBUG_LEVEL_ISR )
		printk(KERN_DEBUG "%s(%d): synclinkmp_interrupt(%d)entry.\n",
			__FILE__, __LINE__, info->irq_level);

	spin_lock(&info->lock);

	for(;;) {

		/* get status for SCA0 (ports 0-1) */
		tmp = read_reg16(info, ISR0);	/* get ISR0 and ISR1 in one read */
		status0 = (unsigned char)tmp;
		dmastatus0 = (unsigned char)(tmp>>8);
		timerstatus0 = read_reg(info, ISR2);

		if ( debug_level >= DEBUG_LEVEL_ISR )
			printk(KERN_DEBUG "%s(%d):%s status0=%02x, dmastatus0=%02x, timerstatus0=%02x\n",
				__FILE__, __LINE__, info->device_name,
				status0, dmastatus0, timerstatus0);

		if (info->port_count == 4) {
			/* get status for SCA1 (ports 2-3) */
			tmp = read_reg16(info->port_array[2], ISR0);
			status1 = (unsigned char)tmp;
			dmastatus1 = (unsigned char)(tmp>>8);
			timerstatus1 = read_reg(info->port_array[2], ISR2);

			if ( debug_level >= DEBUG_LEVEL_ISR )
				printk("%s(%d):%s status1=%02x, dmastatus1=%02x, timerstatus1=%02x\n",
					__FILE__,__LINE__,info->device_name,
					status1,dmastatus1,timerstatus1);
		}

		if (!status0 && !dmastatus0 && !timerstatus0 &&
			 !status1 && !dmastatus1 && !timerstatus1)
			break;

		for(i=0; i < info->port_count ; i++) {
			if (info->port_array[i] == NULL)
				continue;
			if (i < 2) {
				status = status0;
				dmastatus = dmastatus0;
			} else {
				status = status1;
				dmastatus = dmastatus1;
			}

			shift = i & 1 ? 4 :0;

			if (status & BIT0 << shift)
				isr_rxrdy(info->port_array[i]);
			if (status & BIT1 << shift)
				isr_txrdy(info->port_array[i]);
			if (status & BIT2 << shift)
				isr_rxint(info->port_array[i]);
			if (status & BIT3 << shift)
				isr_txint(info->port_array[i]);

			if (dmastatus & BIT0 << shift)
				isr_rxdmaerror(info->port_array[i]);
			if (dmastatus & BIT1 << shift)
				isr_rxdmaok(info->port_array[i]);
			if (dmastatus & BIT2 << shift)
				isr_txdmaerror(info->port_array[i]);
			if (dmastatus & BIT3 << shift)
				isr_txdmaok(info->port_array[i]);
		}

		if (timerstatus0 & (BIT5 | BIT4))
			isr_timer(info->port_array[0]);
		if (timerstatus0 & (BIT7 | BIT6))
			isr_timer(info->port_array[1]);
		if (timerstatus1 & (BIT5 | BIT4))
			isr_timer(info->port_array[2]);
		if (timerstatus1 & (BIT7 | BIT6))
			isr_timer(info->port_array[3]);
	}

	for(i=0; i < info->port_count ; i++) {
		SLMP_INFO * port = info->port_array[i];

		/* Request bottom half processing if there's something
		 * for it to do and the bh is not already running.
		 *
		 * Note: startup adapter diags require interrupts.
		 * do not request bottom half processing if the
		 * device is not open in a normal mode.
		 */
		if ( port && (port->port.count || port->netcount) &&
		     port->pending_bh && !port->bh_running &&
		     !port->bh_requested ) {
			if ( debug_level >= DEBUG_LEVEL_ISR )
				printk("%s(%d):%s queueing bh task.\n",
					__FILE__,__LINE__,port->device_name);
			schedule_work(&port->task);
			port->bh_requested = true;
		}
	}

	spin_unlock(&info->lock);

	if ( debug_level >= DEBUG_LEVEL_ISR )
		printk(KERN_DEBUG "%s(%d):synclinkmp_interrupt(%d)exit.\n",
			__FILE__, __LINE__, info->irq_level);
	return IRQ_HANDLED;
}

/* Initialize and start device.
 */
static int startup(SLMP_INFO * info)
{
	if ( debug_level >= DEBUG_LEVEL_INFO )
		printk("%s(%d):%s tx_releaseup()\n",__FILE__,__LINE__,info->device_name);

	if (info->port.flags & ASYNC_INITIALIZED)
		return 0;

	if (!info->tx_buf) {
		info->tx_buf = kmalloc(info->max_frame_size, GFP_KERNEL);
		if (!info->tx_buf) {
			printk(KERN_ERR"%s(%d):%s can't allocate transmit buffer\n",
				__FILE__,__LINE__,info->device_name);
			return -ENOMEM;
		}
	}

	info->pending_bh = 0;

	memset(&info->icount, 0, sizeof(info->icount));

	/* program hardware for current parameters */
	reset_port(info);

	change_params(info);

	mod_timer(&info->status_timer, jiffies + msecs_to_jiffies(10));

	if (info->port.tty)
		clear_bit(TTY_IO_ERROR, &info->port.tty->flags);

	info->port.flags |= ASYNC_INITIALIZED;

	return 0;
}

/* Called by close() and hangup() to shutdown hardware
 */
static void shutdown(SLMP_INFO * info)
{
	unsigned long flags;

	if (!(info->port.flags & ASYNC_INITIALIZED))
		return;

	if (debug_level >= DEBUG_LEVEL_INFO)
		printk("%s(%d):%s synclinkmp_shutdown()\n",
			 __FILE__,__LINE__, info->device_name );

	/* clear status wait queue because status changes */
	/* can't happen after shutting down the hardware */
	wake_up_interruptible(&info->status_event_wait_q);
	wake_up_interruptible(&info->event_wait_q);

	del_timer(&info->tx_timer);
	del_timer(&info->status_timer);

	kfree(info->tx_buf);
	info->tx_buf = NULL;

	spin_lock_irqsave(&info->lock,flags);

	reset_port(info);

 	if (!info->port.tty || info->port.tty->termios.c_cflag & HUPCL) {
 		info->serial_signals &= ~(SerialSignal_DTR + SerialSignal_RTS);
		set_signals(info);
	}

	spin_unlock_irqrestore(&info->lock,flags);

	if (info->port.tty)
		set_bit(TTY_IO_ERROR, &info->port.tty->flags);

	info->port.flags &= ~ASYNC_INITIALIZED;
}

static void program_hw(SLMP_INFO *info)
{
	unsigned long flags;

	spin_lock_irqsave(&info->lock,flags);

	rx_stop(info);
	tx_stop(info);

	info->tx_count = info->tx_put = info->tx_get = 0;

	if (info->params.mode == MGSL_MODE_HDLC || info->netcount)
		hdlc_mode(info);
	else
		async_mode(info);

	set_signals(info);

	info->dcd_chkcount = 0;
	info->cts_chkcount = 0;
	info->ri_chkcount = 0;
	info->dsr_chkcount = 0;

	info->ie1_value |= (CDCD|CCTS);
	write_reg(info, IE1, info->ie1_value);

	get_signals(info);

	if (info->netcount || (info->port.tty && info->port.tty->termios.c_cflag & CREAD) )
		rx_start(info);

	spin_unlock_irqrestore(&info->lock,flags);
}

/* Reconfigure adapter based on new parameters
 */
static void change_params(SLMP_INFO *info)
{
	unsigned cflag;
	int bits_per_char;

	if (!info->port.tty)
		return;

	if (debug_level >= DEBUG_LEVEL_INFO)
		printk("%s(%d):%s change_params()\n",
			 __FILE__,__LINE__, info->device_name );

	cflag = info->port.tty->termios.c_cflag;

	/* if B0 rate (hangup) specified then negate DTR and RTS */
	/* otherwise assert DTR and RTS */
 	if (cflag & CBAUD)
		info->serial_signals |= SerialSignal_RTS + SerialSignal_DTR;
	else
		info->serial_signals &= ~(SerialSignal_RTS + SerialSignal_DTR);

	/* byte size and parity */

	switch (cflag & CSIZE) {
	      case CS5: info->params.data_bits = 5; break;
	      case CS6: info->params.data_bits = 6; break;
	      case CS7: info->params.data_bits = 7; break;
	      case CS8: info->params.data_bits = 8; break;
	      /* Never happens, but GCC is too dumb to figure it out */
	      default:  info->params.data_bits = 7; break;
	      }

	if (cflag & CSTOPB)
		info->params.stop_bits = 2;
	else
		info->params.stop_bits = 1;

	info->params.parity = ASYNC_PARITY_NONE;
	if (cflag & PARENB) {
		if (cflag & PARODD)
			info->params.parity = ASYNC_PARITY_ODD;
		else
			info->params.parity = ASYNC_PARITY_EVEN;
#ifdef CMSPAR
		if (cflag & CMSPAR)
			info->params.parity = ASYNC_PARITY_SPACE;
#endif
	}

	/* calculate number of jiffies to transmit a full
	 * FIFO (32 bytes) at specified data rate
	 */
	bits_per_char = info->params.data_bits +
			info->params.stop_bits + 1;

	/* if port data rate is set to 460800 or less then
	 * allow tty settings to override, otherwise keep the
	 * current data rate.
	 */
	if (info->params.data_rate <= 460800) {
		info->params.data_rate = tty_get_baud_rate(info->port.tty);
	}

	if ( info->params.data_rate ) {
		info->timeout = (32*HZ*bits_per_char) /
				info->params.data_rate;
	}
	info->timeout += HZ/50;		/* Add .02 seconds of slop */

	if (cflag & CRTSCTS)
		info->port.flags |= ASYNC_CTS_FLOW;
	else
		info->port.flags &= ~ASYNC_CTS_FLOW;

	if (cflag & CLOCAL)
		info->port.flags &= ~ASYNC_CHECK_CD;
	else
		info->port.flags |= ASYNC_CHECK_CD;

	/* process tty input control flags */

	info->read_status_mask2 = OVRN;
	if (I_INPCK(info->port.tty))
		info->read_status_mask2 |= PE | FRME;
 	if (I_BRKINT(info->port.tty) || I_PARMRK(info->port.tty))
 		info->read_status_mask1 |= BRKD;
	if (I_IGNPAR(info->port.tty))
		info->ignore_status_mask2 |= PE | FRME;
	if (I_IGNBRK(info->port.tty)) {
		info->ignore_status_mask1 |= BRKD;
		/* If ignoring parity and break indicators, ignore
		 * overruns too.  (For real raw support).
		 */
		if (I_IGNPAR(info->port.tty))
			info->ignore_status_mask2 |= OVRN;
	}

	program_hw(info);
}

static int get_stats(SLMP_INFO * info, struct mgsl_icount __user *user_icount)
{
	int err;

	if (debug_level >= DEBUG_LEVEL_INFO)
		printk("%s(%d):%s get_params()\n",
			 __FILE__,__LINE__, info->device_name);

	if (!user_icount) {
		memset(&info->icount, 0, sizeof(info->icount));
	} else {
		mutex_lock(&info->port.mutex);
		COPY_TO_USER(err, user_icount, &info->icount, sizeof(struct mgsl_icount));
		mutex_unlock(&info->port.mutex);
		if (err)
			return -EFAULT;
	}

	return 0;
}

static int get_params(SLMP_INFO * info, MGSL_PARAMS __user *user_params)
{
	int err;
	if (debug_level >= DEBUG_LEVEL_INFO)
		printk("%s(%d):%s get_params()\n",
			 __FILE__,__LINE__, info->device_name);

	mutex_lock(&info->port.mutex);
	COPY_TO_USER(err,user_params, &info->params, sizeof(MGSL_PARAMS));
	mutex_unlock(&info->port.mutex);
	if (err) {
		if ( debug_level >= DEBUG_LEVEL_INFO )
			printk( "%s(%d):%s get_params() user buffer copy failed\n",
				__FILE__,__LINE__,info->device_name);
		return -EFAULT;
	}

	return 0;
}

static int set_params(SLMP_INFO * info, MGSL_PARAMS __user *new_params)
{
 	unsigned long flags;
	MGSL_PARAMS tmp_params;
	int err;

	if (debug_level >= DEBUG_LEVEL_INFO)
		printk("%s(%d):%s set_params\n",
			__FILE__,__LINE__,info->device_name );
	COPY_FROM_USER(err,&tmp_params, new_params, sizeof(MGSL_PARAMS));
	if (err) {
		if ( debug_level >= DEBUG_LEVEL_INFO )
			printk( "%s(%d):%s set_params() user buffer copy failed\n",
				__FILE__,__LINE__,info->device_name);
		return -EFAULT;
	}

	mutex_lock(&info->port.mutex);
	spin_lock_irqsave(&info->lock,flags);
	memcpy(&info->params,&tmp_params,sizeof(MGSL_PARAMS));
	spin_unlock_irqrestore(&info->lock,flags);

 	change_params(info);
	mutex_unlock(&info->port.mutex);

	return 0;
}

static int get_txidle(SLMP_INFO * info, int __user *idle_mode)
{
	int err;

	if (debug_level >= DEBUG_LEVEL_INFO)
		printk("%s(%d):%s get_txidle()=%d\n",
			 __FILE__,__LINE__, info->device_name, info->idle_mode);

	COPY_TO_USER(err,idle_mode, &info->idle_mode, sizeof(int));
	if (err) {
		if ( debug_level >= DEBUG_LEVEL_INFO )
			printk( "%s(%d):%s get_txidle() user buffer copy failed\n",
				__FILE__,__LINE__,info->device_name);
		return -EFAULT;
	}

	return 0;
}

static int set_txidle(SLMP_INFO * info, int idle_mode)
{
 	unsigned long flags;

	if (debug_level >= DEBUG_LEVEL_INFO)
		printk("%s(%d):%s set_txidle(%d)\n",
			__FILE__,__LINE__,info->device_name, idle_mode );

	spin_lock_irqsave(&info->lock,flags);
	info->idle_mode = idle_mode;
	tx_set_idle( info );
	spin_unlock_irqrestore(&info->lock,flags);
	return 0;
}

static int tx_enable(SLMP_INFO * info, int enable)
{
 	unsigned long flags;

	if (debug_level >= DEBUG_LEVEL_INFO)
		printk("%s(%d):%s tx_enable(%d)\n",
			__FILE__,__LINE__,info->device_name, enable);

	spin_lock_irqsave(&info->lock,flags);
	if ( enable ) {
		if ( !info->tx_enabled ) {
			tx_start(info);
		}
	} else {
		if ( info->tx_enabled )
			tx_stop(info);
	}
	spin_unlock_irqrestore(&info->lock,flags);
	return 0;
}

/* abort send HDLC frame
 */
static int tx_abort(SLMP_INFO * info)
{
 	unsigned long flags;

	if (debug_level >= DEBUG_LEVEL_INFO)
		printk("%s(%d):%s tx_abort()\n",
			__FILE__,__LINE__,info->device_name);

	spin_lock_irqsave(&info->lock,flags);
	if ( info->tx_active && info->params.mode == MGSL_MODE_HDLC ) {
		info->ie1_value &= ~UDRN;
		info->ie1_value |= IDLE;
		write_reg(info, IE1, info->ie1_value);	/* disable tx status interrupts */
		write_reg(info, SR1, (unsigned char)(IDLE + UDRN));	/* clear pending */

		write_reg(info, TXDMA + DSR, 0);		/* disable DMA channel */
		write_reg(info, TXDMA + DCMD, SWABORT);	/* reset/init DMA channel */

   		write_reg(info, CMD, TXABORT);
	}
	spin_unlock_irqrestore(&info->lock,flags);
	return 0;
}

static int rx_enable(SLMP_INFO * info, int enable)
{
 	unsigned long flags;

	if (debug_level >= DEBUG_LEVEL_INFO)
		printk("%s(%d):%s rx_enable(%d)\n",
			__FILE__,__LINE__,info->device_name,enable);

	spin_lock_irqsave(&info->lock,flags);
	if ( enable ) {
		if ( !info->rx_enabled )
			rx_start(info);
	} else {
		if ( info->rx_enabled )
			rx_stop(info);
	}
	spin_unlock_irqrestore(&info->lock,flags);
	return 0;
}

/* wait for specified event to occur
 */
static int wait_mgsl_event(SLMP_INFO * info, int __user *mask_ptr)
{
 	unsigned long flags;
	int s;
	int rc=0;
	struct mgsl_icount cprev, cnow;
	int events;
	int mask;
	struct	_input_signal_events oldsigs, newsigs;
	DECLARE_WAITQUEUE(wait, current);

	COPY_FROM_USER(rc,&mask, mask_ptr, sizeof(int));
	if (rc) {
		return  -EFAULT;
	}

	if (debug_level >= DEBUG_LEVEL_INFO)
		printk("%s(%d):%s wait_mgsl_event(%d)\n",
			__FILE__,__LINE__,info->device_name,mask);

	spin_lock_irqsave(&info->lock,flags);

	/* return immediately if state matches requested events */
	get_signals(info);
	s = info->serial_signals;

	events = mask &
		( ((s & SerialSignal_DSR) ? MgslEvent_DsrActive:MgslEvent_DsrInactive) +
 		  ((s & SerialSignal_DCD) ? MgslEvent_DcdActive:MgslEvent_DcdInactive) +
		  ((s & SerialSignal_CTS) ? MgslEvent_CtsActive:MgslEvent_CtsInactive) +
		  ((s & SerialSignal_RI)  ? MgslEvent_RiActive :MgslEvent_RiInactive) );
	if (events) {
		spin_unlock_irqrestore(&info->lock,flags);
		goto exit;
	}

	/* save current irq counts */
	cprev = info->icount;
	oldsigs = info->input_signal_events;

	/* enable hunt and idle irqs if needed */
	if (mask & (MgslEvent_ExitHuntMode+MgslEvent_IdleReceived)) {
		unsigned char oldval = info->ie1_value;
		unsigned char newval = oldval +
			 (mask & MgslEvent_ExitHuntMode ? FLGD:0) +
			 (mask & MgslEvent_IdleReceived ? IDLD:0);
		if ( oldval != newval ) {
			info->ie1_value = newval;
			write_reg(info, IE1, info->ie1_value);
		}
	}

	set_current_state(TASK_INTERRUPTIBLE);
	add_wait_queue(&info->event_wait_q, &wait);

	spin_unlock_irqrestore(&info->lock,flags);

	for(;;) {
		schedule();
		if (signal_pending(current)) {
			rc = -ERESTARTSYS;
			break;
		}

		/* get current irq counts */
		spin_lock_irqsave(&info->lock,flags);
		cnow = info->icount;
		newsigs = info->input_signal_events;
		set_current_state(TASK_INTERRUPTIBLE);
		spin_unlock_irqrestore(&info->lock,flags);

		/* if no change, wait aborted for some reason */
		if (newsigs.dsr_up   == oldsigs.dsr_up   &&
		    newsigs.dsr_down == oldsigs.dsr_down &&
		    newsigs.dcd_up   == oldsigs.dcd_up   &&
		    newsigs.dcd_down == oldsigs.dcd_down &&
		    newsigs.cts_up   == oldsigs.cts_up   &&
		    newsigs.cts_down == oldsigs.cts_down &&
		    newsigs.ri_up    == oldsigs.ri_up    &&
		    newsigs.ri_down  == oldsigs.ri_down  &&
		    cnow.exithunt    == cprev.exithunt   &&
		    cnow.rxidle      == cprev.rxidle) {
			rc = -EIO;
			break;
		}

		events = mask &
			( (newsigs.dsr_up   != oldsigs.dsr_up   ? MgslEvent_DsrActive:0)   +
			  (newsigs.dsr_down != oldsigs.dsr_down ? MgslEvent_DsrInactive:0) +
			  (newsigs.dcd_up   != oldsigs.dcd_up   ? MgslEvent_DcdActive:0)   +
			  (newsigs.dcd_down != oldsigs.dcd_down ? MgslEvent_DcdInactive:0) +
			  (newsigs.cts_up   != oldsigs.cts_up   ? MgslEvent_CtsActive:0)   +
			  (newsigs.cts_down != oldsigs.cts_down ? MgslEvent_CtsInactive:0) +
			  (newsigs.ri_up    != oldsigs.ri_up    ? MgslEvent_RiActive:0)    +
			  (newsigs.ri_down  != oldsigs.ri_down  ? MgslEvent_RiInactive:0)  +
			  (cnow.exithunt    != cprev.exithunt   ? MgslEvent_ExitHuntMode:0) +
			  (cnow.rxidle      != cprev.rxidle     ? MgslEvent_IdleReceived:0) );
		if (events)
			break;

		cprev = cnow;
		oldsigs = newsigs;
	}

	remove_wait_queue(&info->event_wait_q, &wait);
	set_current_state(TASK_RUNNING);


	if (mask & (MgslEvent_ExitHuntMode + MgslEvent_IdleReceived)) {
		spin_lock_irqsave(&info->lock,flags);
		if (!waitqueue_active(&info->event_wait_q)) {
			/* disable enable exit hunt mode/idle rcvd IRQs */
			info->ie1_value &= ~(FLGD|IDLD);
			write_reg(info, IE1, info->ie1_value);
		}
		spin_unlock_irqrestore(&info->lock,flags);
	}
exit:
	if ( rc == 0 )
		PUT_USER(rc, events, mask_ptr);

	return rc;
}

static int modem_input_wait(SLMP_INFO *info,int arg)
{
 	unsigned long flags;
	int rc;
	struct mgsl_icount cprev, cnow;
	DECLARE_WAITQUEUE(wait, current);

	/* save current irq counts */
	spin_lock_irqsave(&info->lock,flags);
	cprev = info->icount;
	add_wait_queue(&info->status_event_wait_q, &wait);
	set_current_state(TASK_INTERRUPTIBLE);
	spin_unlock_irqrestore(&info->lock,flags);

	for(;;) {
		schedule();
		if (signal_pending(current)) {
			rc = -ERESTARTSYS;
			break;
		}

		/* get new irq counts */
		spin_lock_irqsave(&info->lock,flags);
		cnow = info->icount;
		set_current_state(TASK_INTERRUPTIBLE);
		spin_unlock_irqrestore(&info->lock,flags);

		/* if no change, wait aborted for some reason */
		if (cnow.rng == cprev.rng && cnow.dsr == cprev.dsr &&
		    cnow.dcd == cprev.dcd && cnow.cts == cprev.cts) {
			rc = -EIO;
			break;
		}

		/* check for change in caller specified modem input */
		if ((arg & TIOCM_RNG && cnow.rng != cprev.rng) ||
		    (arg & TIOCM_DSR && cnow.dsr != cprev.dsr) ||
		    (arg & TIOCM_CD  && cnow.dcd != cprev.dcd) ||
		    (arg & TIOCM_CTS && cnow.cts != cprev.cts)) {
			rc = 0;
			break;
		}

		cprev = cnow;
	}
	remove_wait_queue(&info->status_event_wait_q, &wait);
	set_current_state(TASK_RUNNING);
	return rc;
}

/* return the state of the serial control and status signals
 */
static int tiocmget(struct tty_struct *tty)
{
	SLMP_INFO *info = tty->driver_data;
	unsigned int result;
 	unsigned long flags;

	spin_lock_irqsave(&info->lock,flags);
 	get_signals(info);
	spin_unlock_irqrestore(&info->lock,flags);

	result = ((info->serial_signals & SerialSignal_RTS) ? TIOCM_RTS:0) +
		((info->serial_signals & SerialSignal_DTR) ? TIOCM_DTR:0) +
		((info->serial_signals & SerialSignal_DCD) ? TIOCM_CAR:0) +
		((info->serial_signals & SerialSignal_RI)  ? TIOCM_RNG:0) +
		((info->serial_signals & SerialSignal_DSR) ? TIOCM_DSR:0) +
		((info->serial_signals & SerialSignal_CTS) ? TIOCM_CTS:0);

	if (debug_level >= DEBUG_LEVEL_INFO)
		printk("%s(%d):%s tiocmget() value=%08X\n",
			 __FILE__,__LINE__, info->device_name, result );
	return result;
}

/* set modem control signals (DTR/RTS)
 */
static int tiocmset(struct tty_struct *tty,
					unsigned int set, unsigned int clear)
{
	SLMP_INFO *info = tty->driver_data;
 	unsigned long flags;

	if (debug_level >= DEBUG_LEVEL_INFO)
		printk("%s(%d):%s tiocmset(%x,%x)\n",
			__FILE__,__LINE__,info->device_name, set, clear);

	if (set & TIOCM_RTS)
		info->serial_signals |= SerialSignal_RTS;
	if (set & TIOCM_DTR)
		info->serial_signals |= SerialSignal_DTR;
	if (clear & TIOCM_RTS)
		info->serial_signals &= ~SerialSignal_RTS;
	if (clear & TIOCM_DTR)
		info->serial_signals &= ~SerialSignal_DTR;

	spin_lock_irqsave(&info->lock,flags);
 	set_signals(info);
	spin_unlock_irqrestore(&info->lock,flags);

	return 0;
}

static int carrier_raised(struct tty_port *port)
{
	SLMP_INFO *info = container_of(port, SLMP_INFO, port);
	unsigned long flags;

	spin_lock_irqsave(&info->lock,flags);
 	get_signals(info);
	spin_unlock_irqrestore(&info->lock,flags);

	return (info->serial_signals & SerialSignal_DCD) ? 1 : 0;
}

static void dtr_rts(struct tty_port *port, int on)
{
	SLMP_INFO *info = container_of(port, SLMP_INFO, port);
	unsigned long flags;

	spin_lock_irqsave(&info->lock,flags);
	if (on)
		info->serial_signals |= SerialSignal_RTS + SerialSignal_DTR;
	else
		info->serial_signals &= ~(SerialSignal_RTS + SerialSignal_DTR);
 	set_signals(info);
	spin_unlock_irqrestore(&info->lock,flags);
}

/* Block the current process until the specified port is ready to open.
 */
static int block_til_ready(struct tty_struct *tty, struct file *filp,
			   SLMP_INFO *info)
{
	DECLARE_WAITQUEUE(wait, current);
	int		retval;
	bool		do_clocal = false;
	bool		extra_count = false;
	unsigned long	flags;
	int		cd;
	struct tty_port *port = &info->port;

	if (debug_level >= DEBUG_LEVEL_INFO)
		printk("%s(%d):%s block_til_ready()\n",
			 __FILE__,__LINE__, tty->driver->name );

	if (filp->f_flags & O_NONBLOCK || tty->flags & (1 << TTY_IO_ERROR)){
		/* nonblock mode is set or port is not enabled */
		/* just verify that callout device is not active */
		port->flags |= ASYNC_NORMAL_ACTIVE;
		return 0;
	}

	if (tty->termios.c_cflag & CLOCAL)
		do_clocal = true;

	/* Wait for carrier detect and the line to become
	 * free (i.e., not in use by the callout).  While we are in
	 * this loop, port->count is dropped by one, so that
	 * close() knows when to free things.  We restore it upon
	 * exit, either normal or abnormal.
	 */

	retval = 0;
	add_wait_queue(&port->open_wait, &wait);

	if (debug_level >= DEBUG_LEVEL_INFO)
		printk("%s(%d):%s block_til_ready() before block, count=%d\n",
			 __FILE__,__LINE__, tty->driver->name, port->count );

	spin_lock_irqsave(&info->lock, flags);
	if (!tty_hung_up_p(filp)) {
		extra_count = true;
		port->count--;
	}
	spin_unlock_irqrestore(&info->lock, flags);
	port->blocked_open++;

	while (1) {
		if (tty->termios.c_cflag & CBAUD)
			tty_port_raise_dtr_rts(port);

		set_current_state(TASK_INTERRUPTIBLE);

		if (tty_hung_up_p(filp) || !(port->flags & ASYNC_INITIALIZED)){
			retval = (port->flags & ASYNC_HUP_NOTIFY) ?
					-EAGAIN : -ERESTARTSYS;
			break;
		}

		cd = tty_port_carrier_raised(port);

 		if (!(port->flags & ASYNC_CLOSING) && (do_clocal || cd))
 			break;

		if (signal_pending(current)) {
			retval = -ERESTARTSYS;
			break;
		}

		if (debug_level >= DEBUG_LEVEL_INFO)
			printk("%s(%d):%s block_til_ready() count=%d\n",
				 __FILE__,__LINE__, tty->driver->name, port->count );

		tty_unlock(tty);
		schedule();
		tty_lock(tty);
	}

	set_current_state(TASK_RUNNING);
	remove_wait_queue(&port->open_wait, &wait);

	if (extra_count)
		port->count++;
	port->blocked_open--;

	if (debug_level >= DEBUG_LEVEL_INFO)
		printk("%s(%d):%s block_til_ready() after, count=%d\n",
			 __FILE__,__LINE__, tty->driver->name, port->count );

	if (!retval)
		port->flags |= ASYNC_NORMAL_ACTIVE;

	return retval;
}

static int alloc_dma_bufs(SLMP_INFO *info)
{
	unsigned short BuffersPerFrame;
	unsigned short BufferCount;

	// Force allocation to start at 64K boundary for each port.
	// This is necessary because *all* buffer descriptors for a port
	// *must* be in the same 64K block. All descriptors on a port
	// share a common 'base' address (upper 8 bits of 24 bits) programmed
	// into the CBP register.
	info->port_array[0]->last_mem_alloc = (SCA_MEM_SIZE/4) * info->port_num;

	/* Calculate the number of DMA buffers necessary to hold the */
	/* largest allowable frame size. Note: If the max frame size is */
	/* not an even multiple of the DMA buffer size then we need to */
	/* round the buffer count per frame up one. */

	BuffersPerFrame = (unsigned short)(info->max_frame_size/SCABUFSIZE);
	if ( info->max_frame_size % SCABUFSIZE )
		BuffersPerFrame++;

	/* calculate total number of data buffers (SCABUFSIZE) possible
	 * in one ports memory (SCA_MEM_SIZE/4) after allocating memory
	 * for the descriptor list (BUFFERLISTSIZE).
	 */
	BufferCount = (SCA_MEM_SIZE/4 - BUFFERLISTSIZE)/SCABUFSIZE;

	/* limit number of buffers to maximum amount of descriptors */
	if (BufferCount > BUFFERLISTSIZE/sizeof(SCADESC))
		BufferCount = BUFFERLISTSIZE/sizeof(SCADESC);

	/* use enough buffers to transmit one max size frame */
	info->tx_buf_count = BuffersPerFrame + 1;

	/* never use more than half the available buffers for transmit */
	if (info->tx_buf_count > (BufferCount/2))
		info->tx_buf_count = BufferCount/2;

	if (info->tx_buf_count > SCAMAXDESC)
		info->tx_buf_count = SCAMAXDESC;

	/* use remaining buffers for receive */
	info->rx_buf_count = BufferCount - info->tx_buf_count;

	if (info->rx_buf_count > SCAMAXDESC)
		info->rx_buf_count = SCAMAXDESC;

	if ( debug_level >= DEBUG_LEVEL_INFO )
		printk("%s(%d):%s Allocating %d TX and %d RX DMA buffers.\n",
			__FILE__,__LINE__, info->device_name,
			info->tx_buf_count,info->rx_buf_count);

	if ( alloc_buf_list( info ) < 0 ||
		alloc_frame_bufs(info,
		  			info->rx_buf_list,
		  			info->rx_buf_list_ex,
					info->rx_buf_count) < 0 ||
		alloc_frame_bufs(info,
					info->tx_buf_list,
					info->tx_buf_list_ex,
					info->tx_buf_count) < 0 ||
		alloc_tmp_rx_buf(info) < 0 ) {
		printk("%s(%d):%s Can't allocate DMA buffer memory\n",
			__FILE__,__LINE__, info->device_name);
		return -ENOMEM;
	}

	rx_reset_buffers( info );

	return 0;
}

/* Allocate DMA buffers for the transmit and receive descriptor lists.
 */
static int alloc_buf_list(SLMP_INFO *info)
{
	unsigned int i;

	/* build list in adapter shared memory */
	info->buffer_list = info->memory_base + info->port_array[0]->last_mem_alloc;
	info->buffer_list_phys = info->port_array[0]->last_mem_alloc;
	info->port_array[0]->last_mem_alloc += BUFFERLISTSIZE;

	memset(info->buffer_list, 0, BUFFERLISTSIZE);

	/* Save virtual address pointers to the receive and */
	/* transmit buffer lists. (Receive 1st). These pointers will */
	/* be used by the processor to access the lists. */
	info->rx_buf_list = (SCADESC *)info->buffer_list;

	info->tx_buf_list = (SCADESC *)info->buffer_list;
	info->tx_buf_list += info->rx_buf_count;

	/* Build links for circular buffer entry lists (tx and rx)
	 *
	 * Note: links are physical addresses read by the SCA device
	 * to determine the next buffer entry to use.
	 */

	for ( i = 0; i < info->rx_buf_count; i++ ) {
		/* calculate and store physical address of this buffer entry */
		info->rx_buf_list_ex[i].phys_entry =
			info->buffer_list_phys + (i * sizeof(SCABUFSIZE));

		/* calculate and store physical address of */
		/* next entry in cirular list of entries */
		info->rx_buf_list[i].next = info->buffer_list_phys;
		if ( i < info->rx_buf_count - 1 )
			info->rx_buf_list[i].next += (i + 1) * sizeof(SCADESC);

		info->rx_buf_list[i].length = SCABUFSIZE;
	}

	for ( i = 0; i < info->tx_buf_count; i++ ) {
		/* calculate and store physical address of this buffer entry */
		info->tx_buf_list_ex[i].phys_entry = info->buffer_list_phys +
			((info->rx_buf_count + i) * sizeof(SCADESC));

		/* calculate and store physical address of */
		/* next entry in cirular list of entries */

		info->tx_buf_list[i].next = info->buffer_list_phys +
			info->rx_buf_count * sizeof(SCADESC);

		if ( i < info->tx_buf_count - 1 )
			info->tx_buf_list[i].next += (i + 1) * sizeof(SCADESC);
	}

	return 0;
}

/* Allocate the frame DMA buffers used by the specified buffer list.
 */
static int alloc_frame_bufs(SLMP_INFO *info, SCADESC *buf_list,SCADESC_EX *buf_list_ex,int count)
{
	int i;
	unsigned long phys_addr;

	for ( i = 0; i < count; i++ ) {
		buf_list_ex[i].virt_addr = info->memory_base + info->port_array[0]->last_mem_alloc;
		phys_addr = info->port_array[0]->last_mem_alloc;
		info->port_array[0]->last_mem_alloc += SCABUFSIZE;

		buf_list[i].buf_ptr  = (unsigned short)phys_addr;
		buf_list[i].buf_base = (unsigned char)(phys_addr >> 16);
	}

	return 0;
}

static void free_dma_bufs(SLMP_INFO *info)
{
	info->buffer_list = NULL;
	info->rx_buf_list = NULL;
	info->tx_buf_list = NULL;
}

/* allocate buffer large enough to hold max_frame_size.
 * This buffer is used to pass an assembled frame to the line discipline.
 */
static int alloc_tmp_rx_buf(SLMP_INFO *info)
{
	info->tmp_rx_buf = kmalloc(info->max_frame_size, GFP_KERNEL);
	if (info->tmp_rx_buf == NULL)
		return -ENOMEM;
	return 0;
}

static void free_tmp_rx_buf(SLMP_INFO *info)
{
	kfree(info->tmp_rx_buf);
	info->tmp_rx_buf = NULL;
}

static int claim_resources(SLMP_INFO *info)
{
	if (request_mem_region(info->phys_memory_base,SCA_MEM_SIZE,"synclinkmp") == NULL) {
		printk( "%s(%d):%s mem addr conflict, Addr=%08X\n",
			__FILE__,__LINE__,info->device_name, info->phys_memory_base);
		info->init_error = DiagStatus_AddressConflict;
		goto errout;
	}
	else
		info->shared_mem_requested = true;

	if (request_mem_region(info->phys_lcr_base + info->lcr_offset,128,"synclinkmp") == NULL) {
		printk( "%s(%d):%s lcr mem addr conflict, Addr=%08X\n",
			__FILE__,__LINE__,info->device_name, info->phys_lcr_base);
		info->init_error = DiagStatus_AddressConflict;
		goto errout;
	}
	else
		info->lcr_mem_requested = true;

	if (request_mem_region(info->phys_sca_base + info->sca_offset,SCA_BASE_SIZE,"synclinkmp") == NULL) {
		printk( "%s(%d):%s sca mem addr conflict, Addr=%08X\n",
			__FILE__,__LINE__,info->device_name, info->phys_sca_base);
		info->init_error = DiagStatus_AddressConflict;
		goto errout;
	}
	else
		info->sca_base_requested = true;

	if (request_mem_region(info->phys_statctrl_base + info->statctrl_offset,SCA_REG_SIZE,"synclinkmp") == NULL) {
		printk( "%s(%d):%s stat/ctrl mem addr conflict, Addr=%08X\n",
			__FILE__,__LINE__,info->device_name, info->phys_statctrl_base);
		info->init_error = DiagStatus_AddressConflict;
		goto errout;
	}
	else
		info->sca_statctrl_requested = true;

	info->memory_base = ioremap_nocache(info->phys_memory_base,
								SCA_MEM_SIZE);
	if (!info->memory_base) {
		printk( "%s(%d):%s Can't map shared memory, MemAddr=%08X\n",
			__FILE__,__LINE__,info->device_name, info->phys_memory_base );
		info->init_error = DiagStatus_CantAssignPciResources;
		goto errout;
	}

	info->lcr_base = ioremap_nocache(info->phys_lcr_base, PAGE_SIZE);
	if (!info->lcr_base) {
		printk( "%s(%d):%s Can't map LCR memory, MemAddr=%08X\n",
			__FILE__,__LINE__,info->device_name, info->phys_lcr_base );
		info->init_error = DiagStatus_CantAssignPciResources;
		goto errout;
	}
	info->lcr_base += info->lcr_offset;

	info->sca_base = ioremap_nocache(info->phys_sca_base, PAGE_SIZE);
	if (!info->sca_base) {
		printk( "%s(%d):%s Can't map SCA memory, MemAddr=%08X\n",
			__FILE__,__LINE__,info->device_name, info->phys_sca_base );
		info->init_error = DiagStatus_CantAssignPciResources;
		goto errout;
	}
	info->sca_base += info->sca_offset;

	info->statctrl_base = ioremap_nocache(info->phys_statctrl_base,
								PAGE_SIZE);
	if (!info->statctrl_base) {
		printk( "%s(%d):%s Can't map SCA Status/Control memory, MemAddr=%08X\n",
			__FILE__,__LINE__,info->device_name, info->phys_statctrl_base );
		info->init_error = DiagStatus_CantAssignPciResources;
		goto errout;
	}
	info->statctrl_base += info->statctrl_offset;

	if ( !memory_test(info) ) {
		printk( "%s(%d):Shared Memory Test failed for device %s MemAddr=%08X\n",
			__FILE__,__LINE__,info->device_name, info->phys_memory_base );
		info->init_error = DiagStatus_MemoryError;
		goto errout;
	}

	return 0;

errout:
	release_resources( info );
	return -ENODEV;
}

static void release_resources(SLMP_INFO *info)
{
	if ( debug_level >= DEBUG_LEVEL_INFO )
		printk( "%s(%d):%s release_resources() entry\n",
			__FILE__,__LINE__,info->device_name );

	if ( info->irq_requested ) {
		free_irq(info->irq_level, info);
		info->irq_requested = false;
	}

	if ( info->shared_mem_requested ) {
		release_mem_region(info->phys_memory_base,SCA_MEM_SIZE);
		info->shared_mem_requested = false;
	}
	if ( info->lcr_mem_requested ) {
		release_mem_region(info->phys_lcr_base + info->lcr_offset,128);
		info->lcr_mem_requested = false;
	}
	if ( info->sca_base_requested ) {
		release_mem_region(info->phys_sca_base + info->sca_offset,SCA_BASE_SIZE);
		info->sca_base_requested = false;
	}
	if ( info->sca_statctrl_requested ) {
		release_mem_region(info->phys_statctrl_base + info->statctrl_offset,SCA_REG_SIZE);
		info->sca_statctrl_requested = false;
	}

	if (info->memory_base){
		iounmap(info->memory_base);
		info->memory_base = NULL;
	}

	if (info->sca_base) {
		iounmap(info->sca_base - info->sca_offset);
		info->sca_base=NULL;
	}

	if (info->statctrl_base) {
		iounmap(info->statctrl_base - info->statctrl_offset);
		info->statctrl_base=NULL;
	}

	if (info->lcr_base){
		iounmap(info->lcr_base - info->lcr_offset);
		info->lcr_base = NULL;
	}

	if ( debug_level >= DEBUG_LEVEL_INFO )
		printk( "%s(%d):%s release_resources() exit\n",
			__FILE__,__LINE__,info->device_name );
}

/* Add the specified device instance data structure to the
 * global linked list of devices and increment the device count.
 */
static void add_device(SLMP_INFO *info)
{
	info->next_device = NULL;
	info->line = synclinkmp_device_count;
	sprintf(info->device_name,"ttySLM%dp%d",info->adapter_num,info->port_num);

	if (info->line < MAX_DEVICES) {
		if (maxframe[info->line])
			info->max_frame_size = maxframe[info->line];
	}

	synclinkmp_device_count++;

	if ( !synclinkmp_device_list )
		synclinkmp_device_list = info;
	else {
		SLMP_INFO *current_dev = synclinkmp_device_list;
		while( current_dev->next_device )
			current_dev = current_dev->next_device;
		current_dev->next_device = info;
	}

	if ( info->max_frame_size < 4096 )
		info->max_frame_size = 4096;
	else if ( info->max_frame_size > 65535 )
		info->max_frame_size = 65535;

	printk( "SyncLink MultiPort %s: "
		"Mem=(%08x %08X %08x %08X) IRQ=%d MaxFrameSize=%u\n",
		info->device_name,
		info->phys_sca_base,
		info->phys_memory_base,
		info->phys_statctrl_base,
		info->phys_lcr_base,
		info->irq_level,
		info->max_frame_size );

#if SYNCLINK_GENERIC_HDLC
	hdlcdev_init(info);
#endif
}

static const struct tty_port_operations port_ops = {
	.carrier_raised = carrier_raised,
	.dtr_rts = dtr_rts,
};

/* Allocate and initialize a device instance structure
 *
 * Return Value:	pointer to SLMP_INFO if success, otherwise NULL
 */
static SLMP_INFO *alloc_dev(int adapter_num, int port_num, struct pci_dev *pdev)
{
	SLMP_INFO *info;

	info = kzalloc(sizeof(SLMP_INFO),
		 GFP_KERNEL);

	if (!info) {
		printk("%s(%d) Error can't allocate device instance data for adapter %d, port %d\n",
			__FILE__,__LINE__, adapter_num, port_num);
	} else {
		tty_port_init(&info->port);
		info->port.ops = &port_ops;
		info->magic = MGSL_MAGIC;
		INIT_WORK(&info->task, bh_handler);
		info->max_frame_size = 4096;
		info->port.close_delay = 5*HZ/10;
		info->port.closing_wait = 30*HZ;
		init_waitqueue_head(&info->status_event_wait_q);
		init_waitqueue_head(&info->event_wait_q);
		spin_lock_init(&info->netlock);
		memcpy(&info->params,&default_params,sizeof(MGSL_PARAMS));
		info->idle_mode = HDLC_TXIDLE_FLAGS;
		info->adapter_num = adapter_num;
		info->port_num = port_num;

		/* Copy configuration info to device instance data */
		info->irq_level = pdev->irq;
		info->phys_lcr_base = pci_resource_start(pdev,0);
		info->phys_sca_base = pci_resource_start(pdev,2);
		info->phys_memory_base = pci_resource_start(pdev,3);
		info->phys_statctrl_base = pci_resource_start(pdev,4);

		/* Because veremap only works on page boundaries we must map
		 * a larger area than is actually implemented for the LCR
		 * memory range. We map a full page starting at the page boundary.
		 */
		info->lcr_offset    = info->phys_lcr_base & (PAGE_SIZE-1);
		info->phys_lcr_base &= ~(PAGE_SIZE-1);

		info->sca_offset    = info->phys_sca_base & (PAGE_SIZE-1);
		info->phys_sca_base &= ~(PAGE_SIZE-1);

		info->statctrl_offset    = info->phys_statctrl_base & (PAGE_SIZE-1);
		info->phys_statctrl_base &= ~(PAGE_SIZE-1);

		info->bus_type = MGSL_BUS_TYPE_PCI;
		info->irq_flags = IRQF_SHARED;

		setup_timer(&info->tx_timer, tx_timeout, (unsigned long)info);
		setup_timer(&info->status_timer, status_timeout,
				(unsigned long)info);

		/* Store the PCI9050 misc control register value because a flaw
		 * in the PCI9050 prevents LCR registers from being read if
		 * BIOS assigns an LCR base address with bit 7 set.
		 *
		 * Only the misc control register is accessed for which only
		 * write access is needed, so set an initial value and change
		 * bits to the device instance data as we write the value
		 * to the actual misc control register.
		 */
		info->misc_ctrl_value = 0x087e4546;

		/* initial port state is unknown - if startup errors
		 * occur, init_error will be set to indicate the
		 * problem. Once the port is fully initialized,
		 * this value will be set to 0 to indicate the
		 * port is available.
		 */
		info->init_error = -1;
	}

	return info;
}

static void device_init(int adapter_num, struct pci_dev *pdev)
{
	SLMP_INFO *port_array[SCA_MAX_PORTS];
	int port;

	/* allocate device instances for up to SCA_MAX_PORTS devices */
	for ( port = 0; port < SCA_MAX_PORTS; ++port ) {
		port_array[port] = alloc_dev(adapter_num,port,pdev);
		if( port_array[port] == NULL ) {
			for ( --port; port >= 0; --port )
				kfree(port_array[port]);
			return;
		}
	}

	/* give copy of port_array to all ports and add to device list  */
	for ( port = 0; port < SCA_MAX_PORTS; ++port ) {
		memcpy(port_array[port]->port_array,port_array,sizeof(port_array));
		add_device( port_array[port] );
		spin_lock_init(&port_array[port]->lock);
	}

	/* Allocate and claim adapter resources */
	if ( !claim_resources(port_array[0]) ) {

		alloc_dma_bufs(port_array[0]);

		/* copy resource information from first port to others */
		for ( port = 1; port < SCA_MAX_PORTS; ++port ) {
			port_array[port]->lock  = port_array[0]->lock;
			port_array[port]->irq_level     = port_array[0]->irq_level;
			port_array[port]->memory_base   = port_array[0]->memory_base;
			port_array[port]->sca_base      = port_array[0]->sca_base;
			port_array[port]->statctrl_base = port_array[0]->statctrl_base;
			port_array[port]->lcr_base      = port_array[0]->lcr_base;
			alloc_dma_bufs(port_array[port]);
		}

		if ( request_irq(port_array[0]->irq_level,
					synclinkmp_interrupt,
					port_array[0]->irq_flags,
					port_array[0]->device_name,
					port_array[0]) < 0 ) {
			printk( "%s(%d):%s Can't request interrupt, IRQ=%d\n",
				__FILE__,__LINE__,
				port_array[0]->device_name,
				port_array[0]->irq_level );
		}
		else {
			port_array[0]->irq_requested = true;
			adapter_test(port_array[0]);
		}
	}
}

static const struct tty_operations ops = {
	.install = install,
	.open = open,
	.close = close,
	.write = write,
	.put_char = put_char,
	.flush_chars = flush_chars,
	.write_room = write_room,
	.chars_in_buffer = chars_in_buffer,
	.flush_buffer = flush_buffer,
	.ioctl = ioctl,
	.throttle = throttle,
	.unthrottle = unthrottle,
	.send_xchar = send_xchar,
	.break_ctl = set_break,
	.wait_until_sent = wait_until_sent,
	.set_termios = set_termios,
	.stop = tx_hold,
	.start = tx_release,
	.hangup = hangup,
	.tiocmget = tiocmget,
	.tiocmset = tiocmset,
	.get_icount = get_icount,
	.proc_fops = &synclinkmp_proc_fops,
};


static void synclinkmp_cleanup(void)
{
	int rc;
	SLMP_INFO *info;
	SLMP_INFO *tmp;

	printk("Unloading %s %s\n", driver_name, driver_version);

	if (serial_driver) {
		if ((rc = tty_unregister_driver(serial_driver)))
			printk("%s(%d) failed to unregister tty driver err=%d\n",
			       __FILE__,__LINE__,rc);
		put_tty_driver(serial_driver);
	}

	/* reset devices */
	info = synclinkmp_device_list;
	while(info) {
		reset_port(info);
		info = info->next_device;
	}

	/* release devices */
	info = synclinkmp_device_list;
	while(info) {
#if SYNCLINK_GENERIC_HDLC
		hdlcdev_exit(info);
#endif
		free_dma_bufs(info);
		free_tmp_rx_buf(info);
		if ( info->port_num == 0 ) {
			if (info->sca_base)
				write_reg(info, LPR, 1); /* set low power mode */
			release_resources(info);
		}
		tmp = info;
		info = info->next_device;
		kfree(tmp);
	}

	pci_unregister_driver(&synclinkmp_pci_driver);
}

/* Driver initialization entry point.
 */

static int __init synclinkmp_init(void)
{
	int rc;

	if (break_on_load) {
	 	synclinkmp_get_text_ptr();
  		BREAKPOINT();
	}

 	printk("%s %s\n", driver_name, driver_version);

	if ((rc = pci_register_driver(&synclinkmp_pci_driver)) < 0) {
		printk("%s:failed to register PCI driver, error=%d\n",__FILE__,rc);
		return rc;
	}

	serial_driver = alloc_tty_driver(128);
	if (!serial_driver) {
		rc = -ENOMEM;
		goto error;
	}

	/* Initialize the tty_driver structure */

	serial_driver->driver_name = "synclinkmp";
	serial_driver->name = "ttySLM";
	serial_driver->major = ttymajor;
	serial_driver->minor_start = 64;
	serial_driver->type = TTY_DRIVER_TYPE_SERIAL;
	serial_driver->subtype = SERIAL_TYPE_NORMAL;
	serial_driver->init_termios = tty_std_termios;
	serial_driver->init_termios.c_cflag =
		B9600 | CS8 | CREAD | HUPCL | CLOCAL;
	serial_driver->init_termios.c_ispeed = 9600;
	serial_driver->init_termios.c_ospeed = 9600;
	serial_driver->flags = TTY_DRIVER_REAL_RAW;
	tty_set_operations(serial_driver, &ops);
	if ((rc = tty_register_driver(serial_driver)) < 0) {
		printk("%s(%d):Couldn't register serial driver\n",
			__FILE__,__LINE__);
		put_tty_driver(serial_driver);
		serial_driver = NULL;
		goto error;
	}

 	printk("%s %s, tty major#%d\n",
		driver_name, driver_version,
		serial_driver->major);

	return 0;

error:
	synclinkmp_cleanup();
	return rc;
}

static void __exit synclinkmp_exit(void)
{
	synclinkmp_cleanup();
}

module_init(synclinkmp_init);
module_exit(synclinkmp_exit);

/* Set the port for internal loopback mode.
 * The TxCLK and RxCLK signals are generated from the BRG and
 * the TxD is looped back to the RxD internally.
 */
static void enable_loopback(SLMP_INFO *info, int enable)
{
	if (enable) {
		/* MD2 (Mode Register 2)
		 * 01..00  CNCT<1..0> Channel Connection 11=Local Loopback
		 */
		write_reg(info, MD2, (unsigned char)(read_reg(info, MD2) | (BIT1 + BIT0)));

		/* degate external TxC clock source */
		info->port_array[0]->ctrlreg_value |= (BIT0 << (info->port_num * 2));
		write_control_reg(info);

		/* RXS/TXS (Rx/Tx clock source)
		 * 07      Reserved, must be 0
		 * 06..04  Clock Source, 100=BRG
		 * 03..00  Clock Divisor, 0000=1
		 */
		write_reg(info, RXS, 0x40);
		write_reg(info, TXS, 0x40);

	} else {
		/* MD2 (Mode Register 2)
	 	 * 01..00  CNCT<1..0> Channel connection, 0=normal
		 */
		write_reg(info, MD2, (unsigned char)(read_reg(info, MD2) & ~(BIT1 + BIT0)));

		/* RXS/TXS (Rx/Tx clock source)
		 * 07      Reserved, must be 0
		 * 06..04  Clock Source, 000=RxC/TxC Pin
		 * 03..00  Clock Divisor, 0000=1
		 */
		write_reg(info, RXS, 0x00);
		write_reg(info, TXS, 0x00);
	}

	/* set LinkSpeed if available, otherwise default to 2Mbps */
	if (info->params.clock_speed)
		set_rate(info, info->params.clock_speed);
	else
		set_rate(info, 3686400);
}

/* Set the baud rate register to the desired speed
 *
 *	data_rate	data rate of clock in bits per second
 *			A data rate of 0 disables the AUX clock.
 */
static void set_rate( SLMP_INFO *info, u32 data_rate )
{
       	u32 TMCValue;
       	unsigned char BRValue;
	u32 Divisor=0;

	/* fBRG = fCLK/(TMC * 2^BR)
	 */
	if (data_rate != 0) {
		Divisor = 14745600/data_rate;
		if (!Divisor)
			Divisor = 1;

		TMCValue = Divisor;

		BRValue = 0;
		if (TMCValue != 1 && TMCValue != 2) {
			/* BRValue of 0 provides 50/50 duty cycle *only* when
			 * TMCValue is 1 or 2. BRValue of 1 to 9 always provides
			 * 50/50 duty cycle.
			 */
			BRValue = 1;
			TMCValue >>= 1;
		}

		/* while TMCValue is too big for TMC register, divide
		 * by 2 and increment BR exponent.
		 */
		for(; TMCValue > 256 && BRValue < 10; BRValue++)
			TMCValue >>= 1;

		write_reg(info, TXS,
			(unsigned char)((read_reg(info, TXS) & 0xf0) | BRValue));
		write_reg(info, RXS,
			(unsigned char)((read_reg(info, RXS) & 0xf0) | BRValue));
		write_reg(info, TMC, (unsigned char)TMCValue);
	}
	else {
		write_reg(info, TXS,0);
		write_reg(info, RXS,0);
		write_reg(info, TMC, 0);
	}
}

/* Disable receiver
 */
static void rx_stop(SLMP_INFO *info)
{
	if (debug_level >= DEBUG_LEVEL_ISR)
		printk("%s(%d):%s rx_stop()\n",
			 __FILE__,__LINE__, info->device_name );

	write_reg(info, CMD, RXRESET);

	info->ie0_value &= ~RXRDYE;
	write_reg(info, IE0, info->ie0_value);	/* disable Rx data interrupts */

	write_reg(info, RXDMA + DSR, 0);	/* disable Rx DMA */
	write_reg(info, RXDMA + DCMD, SWABORT);	/* reset/init Rx DMA */
	write_reg(info, RXDMA + DIR, 0);	/* disable Rx DMA interrupts */

	info->rx_enabled = false;
	info->rx_overflow = false;
}

/* enable the receiver
 */
static void rx_start(SLMP_INFO *info)
{
	int i;

	if (debug_level >= DEBUG_LEVEL_ISR)
		printk("%s(%d):%s rx_start()\n",
			 __FILE__,__LINE__, info->device_name );

	write_reg(info, CMD, RXRESET);

	if ( info->params.mode == MGSL_MODE_HDLC ) {
		/* HDLC, disabe IRQ on rxdata */
		info->ie0_value &= ~RXRDYE;
		write_reg(info, IE0, info->ie0_value);

		/* Reset all Rx DMA buffers and program rx dma */
		write_reg(info, RXDMA + DSR, 0);		/* disable Rx DMA */
		write_reg(info, RXDMA + DCMD, SWABORT);	/* reset/init Rx DMA */

		for (i = 0; i < info->rx_buf_count; i++) {
			info->rx_buf_list[i].status = 0xff;

			// throttle to 4 shared memory writes at a time to prevent
			// hogging local bus (keep latency time for DMA requests low).
			if (!(i % 4))
				read_status_reg(info);
		}
		info->current_rx_buf = 0;

		/* set current/1st descriptor address */
		write_reg16(info, RXDMA + CDA,
			info->rx_buf_list_ex[0].phys_entry);

		/* set new last rx descriptor address */
		write_reg16(info, RXDMA + EDA,
			info->rx_buf_list_ex[info->rx_buf_count - 1].phys_entry);

		/* set buffer length (shared by all rx dma data buffers) */
		write_reg16(info, RXDMA + BFL, SCABUFSIZE);

		write_reg(info, RXDMA + DIR, 0x60);	/* enable Rx DMA interrupts (EOM/BOF) */
		write_reg(info, RXDMA + DSR, 0xf2);	/* clear Rx DMA IRQs, enable Rx DMA */
	} else {
		/* async, enable IRQ on rxdata */
		info->ie0_value |= RXRDYE;
		write_reg(info, IE0, info->ie0_value);
	}

	write_reg(info, CMD, RXENABLE);

	info->rx_overflow = false;
	info->rx_enabled = true;
}

/* Enable the transmitter and send a transmit frame if
 * one is loaded in the DMA buffers.
 */
static void tx_start(SLMP_INFO *info)
{
	if (debug_level >= DEBUG_LEVEL_ISR)
		printk("%s(%d):%s tx_start() tx_count=%d\n",
			 __FILE__,__LINE__, info->device_name,info->tx_count );

	if (!info->tx_enabled ) {
		write_reg(info, CMD, TXRESET);
		write_reg(info, CMD, TXENABLE);
		info->tx_enabled = true;
	}

	if ( info->tx_count ) {

		/* If auto RTS enabled and RTS is inactive, then assert */
		/* RTS and set a flag indicating that the driver should */
		/* negate RTS when the transmission completes. */

		info->drop_rts_on_tx_done = false;

		if (info->params.mode != MGSL_MODE_ASYNC) {

			if ( info->params.flags & HDLC_FLAG_AUTO_RTS ) {
				get_signals( info );
				if ( !(info->serial_signals & SerialSignal_RTS) ) {
					info->serial_signals |= SerialSignal_RTS;
					set_signals( info );
					info->drop_rts_on_tx_done = true;
				}
			}

			write_reg16(info, TRC0,
				(unsigned short)(((tx_negate_fifo_level-1)<<8) + tx_active_fifo_level));

			write_reg(info, TXDMA + DSR, 0); 		/* disable DMA channel */
			write_reg(info, TXDMA + DCMD, SWABORT);	/* reset/init DMA channel */
	
			/* set TX CDA (current descriptor address) */
			write_reg16(info, TXDMA + CDA,
				info->tx_buf_list_ex[0].phys_entry);
	
			/* set TX EDA (last descriptor address) */
			write_reg16(info, TXDMA + EDA,
				info->tx_buf_list_ex[info->last_tx_buf].phys_entry);
	
			/* enable underrun IRQ */
			info->ie1_value &= ~IDLE;
			info->ie1_value |= UDRN;
			write_reg(info, IE1, info->ie1_value);
			write_reg(info, SR1, (unsigned char)(IDLE + UDRN));
	
			write_reg(info, TXDMA + DIR, 0x40);		/* enable Tx DMA interrupts (EOM) */
			write_reg(info, TXDMA + DSR, 0xf2);		/* clear Tx DMA IRQs, enable Tx DMA */
	
			mod_timer(&info->tx_timer, jiffies +
					msecs_to_jiffies(5000));
		}
		else {
			tx_load_fifo(info);
			/* async, enable IRQ on txdata */
			info->ie0_value |= TXRDYE;
			write_reg(info, IE0, info->ie0_value);
		}

		info->tx_active = true;
	}
}

/* stop the transmitter and DMA
 */
static void tx_stop( SLMP_INFO *info )
{
	if (debug_level >= DEBUG_LEVEL_ISR)
		printk("%s(%d):%s tx_stop()\n",
			 __FILE__,__LINE__, info->device_name );

	del_timer(&info->tx_timer);

	write_reg(info, TXDMA + DSR, 0);		/* disable DMA channel */
	write_reg(info, TXDMA + DCMD, SWABORT);	/* reset/init DMA channel */

	write_reg(info, CMD, TXRESET);

	info->ie1_value &= ~(UDRN + IDLE);
	write_reg(info, IE1, info->ie1_value);	/* disable tx status interrupts */
	write_reg(info, SR1, (unsigned char)(IDLE + UDRN));	/* clear pending */

	info->ie0_value &= ~TXRDYE;
	write_reg(info, IE0, info->ie0_value);	/* disable tx data interrupts */

	info->tx_enabled = false;
	info->tx_active = false;
}

/* Fill the transmit FIFO until the FIFO is full or
 * there is no more data to load.
 */
static void tx_load_fifo(SLMP_INFO *info)
{
	u8 TwoBytes[2];

	/* do nothing is now tx data available and no XON/XOFF pending */

	if ( !info->tx_count && !info->x_char )
		return;

	/* load the Transmit FIFO until FIFOs full or all data sent */

	while( info->tx_count && (read_reg(info,SR0) & BIT1) ) {

		/* there is more space in the transmit FIFO and */
		/* there is more data in transmit buffer */

		if ( (info->tx_count > 1) && !info->x_char ) {
 			/* write 16-bits */
			TwoBytes[0] = info->tx_buf[info->tx_get++];
			if (info->tx_get >= info->max_frame_size)
				info->tx_get -= info->max_frame_size;
			TwoBytes[1] = info->tx_buf[info->tx_get++];
			if (info->tx_get >= info->max_frame_size)
				info->tx_get -= info->max_frame_size;

			write_reg16(info, TRB, *((u16 *)TwoBytes));

			info->tx_count -= 2;
			info->icount.tx += 2;
		} else {
			/* only 1 byte left to transmit or 1 FIFO slot left */

			if (info->x_char) {
				/* transmit pending high priority char */
				write_reg(info, TRB, info->x_char);
				info->x_char = 0;
			} else {
				write_reg(info, TRB, info->tx_buf[info->tx_get++]);
				if (info->tx_get >= info->max_frame_size)
					info->tx_get -= info->max_frame_size;
				info->tx_count--;
			}
			info->icount.tx++;
		}
	}
}

/* Reset a port to a known state
 */
static void reset_port(SLMP_INFO *info)
{
	if (info->sca_base) {

		tx_stop(info);
		rx_stop(info);

		info->serial_signals &= ~(SerialSignal_DTR + SerialSignal_RTS);
		set_signals(info);

		/* disable all port interrupts */
		info->ie0_value = 0;
		info->ie1_value = 0;
		info->ie2_value = 0;
		write_reg(info, IE0, info->ie0_value);
		write_reg(info, IE1, info->ie1_value);
		write_reg(info, IE2, info->ie2_value);

		write_reg(info, CMD, CHRESET);
	}
}

/* Reset all the ports to a known state.
 */
static void reset_adapter(SLMP_INFO *info)
{
	int i;

	for ( i=0; i < SCA_MAX_PORTS; ++i) {
		if (info->port_array[i])
			reset_port(info->port_array[i]);
	}
}

/* Program port for asynchronous communications.
 */
static void async_mode(SLMP_INFO *info)
{

  	unsigned char RegValue;

	tx_stop(info);
	rx_stop(info);

	/* MD0, Mode Register 0
	 *
	 * 07..05  PRCTL<2..0>, Protocol Mode, 000=async
	 * 04      AUTO, Auto-enable (RTS/CTS/DCD)
	 * 03      Reserved, must be 0
	 * 02      CRCCC, CRC Calculation, 0=disabled
	 * 01..00  STOP<1..0> Stop bits (00=1,10=2)
	 *
	 * 0000 0000
	 */
	RegValue = 0x00;
	if (info->params.stop_bits != 1)
		RegValue |= BIT1;
	write_reg(info, MD0, RegValue);

	/* MD1, Mode Register 1
	 *
	 * 07..06  BRATE<1..0>, bit rate, 00=1/1 01=1/16 10=1/32 11=1/64
	 * 05..04  TXCHR<1..0>, tx char size, 00=8 bits,01=7,10=6,11=5
	 * 03..02  RXCHR<1..0>, rx char size
	 * 01..00  PMPM<1..0>, Parity mode, 00=none 10=even 11=odd
	 *
	 * 0100 0000
	 */
	RegValue = 0x40;
	switch (info->params.data_bits) {
	case 7: RegValue |= BIT4 + BIT2; break;
	case 6: RegValue |= BIT5 + BIT3; break;
	case 5: RegValue |= BIT5 + BIT4 + BIT3 + BIT2; break;
	}
	if (info->params.parity != ASYNC_PARITY_NONE) {
		RegValue |= BIT1;
		if (info->params.parity == ASYNC_PARITY_ODD)
			RegValue |= BIT0;
	}
	write_reg(info, MD1, RegValue);

	/* MD2, Mode Register 2
	 *
	 * 07..02  Reserved, must be 0
	 * 01..00  CNCT<1..0> Channel connection, 00=normal 11=local loopback
	 *
	 * 0000 0000
	 */
	RegValue = 0x00;
	if (info->params.loopback)
		RegValue |= (BIT1 + BIT0);
	write_reg(info, MD2, RegValue);

	/* RXS, Receive clock source
	 *
	 * 07      Reserved, must be 0
	 * 06..04  RXCS<2..0>, clock source, 000=RxC Pin, 100=BRG, 110=DPLL
	 * 03..00  RXBR<3..0>, rate divisor, 0000=1
	 */
	RegValue=BIT6;
	write_reg(info, RXS, RegValue);

	/* TXS, Transmit clock source
	 *
	 * 07      Reserved, must be 0
	 * 06..04  RXCS<2..0>, clock source, 000=TxC Pin, 100=BRG, 110=Receive Clock
	 * 03..00  RXBR<3..0>, rate divisor, 0000=1
	 */
	RegValue=BIT6;
	write_reg(info, TXS, RegValue);

	/* Control Register
	 *
	 * 6,4,2,0  CLKSEL<3..0>, 0 = TcCLK in, 1 = Auxclk out
	 */
	info->port_array[0]->ctrlreg_value |= (BIT0 << (info->port_num * 2));
	write_control_reg(info);

	tx_set_idle(info);

	/* RRC Receive Ready Control 0
	 *
	 * 07..05  Reserved, must be 0
	 * 04..00  RRC<4..0> Rx FIFO trigger active 0x00 = 1 byte
	 */
	write_reg(info, RRC, 0x00);

	/* TRC0 Transmit Ready Control 0
	 *
	 * 07..05  Reserved, must be 0
	 * 04..00  TRC<4..0> Tx FIFO trigger active 0x10 = 16 bytes
	 */
	write_reg(info, TRC0, 0x10);

	/* TRC1 Transmit Ready Control 1
	 *
	 * 07..05  Reserved, must be 0
	 * 04..00  TRC<4..0> Tx FIFO trigger inactive 0x1e = 31 bytes (full-1)
	 */
	write_reg(info, TRC1, 0x1e);

	/* CTL, MSCI control register
	 *
	 * 07..06  Reserved, set to 0
	 * 05      UDRNC, underrun control, 0=abort 1=CRC+flag (HDLC/BSC)
	 * 04      IDLC, idle control, 0=mark 1=idle register
	 * 03      BRK, break, 0=off 1 =on (async)
	 * 02      SYNCLD, sync char load enable (BSC) 1=enabled
	 * 01      GOP, go active on poll (LOOP mode) 1=enabled
	 * 00      RTS, RTS output control, 0=active 1=inactive
	 *
	 * 0001 0001
	 */
	RegValue = 0x10;
	if (!(info->serial_signals & SerialSignal_RTS))
		RegValue |= 0x01;
	write_reg(info, CTL, RegValue);

	/* enable status interrupts */
	info->ie0_value |= TXINTE + RXINTE;
	write_reg(info, IE0, info->ie0_value);

	/* enable break detect interrupt */
	info->ie1_value = BRKD;
	write_reg(info, IE1, info->ie1_value);

	/* enable rx overrun interrupt */
	info->ie2_value = OVRN;
	write_reg(info, IE2, info->ie2_value);

	set_rate( info, info->params.data_rate * 16 );
}

/* Program the SCA for HDLC communications.
 */
static void hdlc_mode(SLMP_INFO *info)
{
	unsigned char RegValue;
	u32 DpllDivisor;

	// Can't use DPLL because SCA outputs recovered clock on RxC when
	// DPLL mode selected. This causes output contention with RxC receiver.
	// Use of DPLL would require external hardware to disable RxC receiver
	// when DPLL mode selected.
	info->params.flags &= ~(HDLC_FLAG_TXC_DPLL + HDLC_FLAG_RXC_DPLL);

	/* disable DMA interrupts */
	write_reg(info, TXDMA + DIR, 0);
	write_reg(info, RXDMA + DIR, 0);

	/* MD0, Mode Register 0
	 *
	 * 07..05  PRCTL<2..0>, Protocol Mode, 100=HDLC
	 * 04      AUTO, Auto-enable (RTS/CTS/DCD)
	 * 03      Reserved, must be 0
	 * 02      CRCCC, CRC Calculation, 1=enabled
	 * 01      CRC1, CRC selection, 0=CRC-16,1=CRC-CCITT-16
	 * 00      CRC0, CRC initial value, 1 = all 1s
	 *
	 * 1000 0001
	 */
	RegValue = 0x81;
	if (info->params.flags & HDLC_FLAG_AUTO_CTS)
		RegValue |= BIT4;
	if (info->params.flags & HDLC_FLAG_AUTO_DCD)
		RegValue |= BIT4;
	if (info->params.crc_type == HDLC_CRC_16_CCITT)
		RegValue |= BIT2 + BIT1;
	write_reg(info, MD0, RegValue);

	/* MD1, Mode Register 1
	 *
	 * 07..06  ADDRS<1..0>, Address detect, 00=no addr check
	 * 05..04  TXCHR<1..0>, tx char size, 00=8 bits
	 * 03..02  RXCHR<1..0>, rx char size, 00=8 bits
	 * 01..00  PMPM<1..0>, Parity mode, 00=no parity
	 *
	 * 0000 0000
	 */
	RegValue = 0x00;
	write_reg(info, MD1, RegValue);

	/* MD2, Mode Register 2
	 *
	 * 07      NRZFM, 0=NRZ, 1=FM
	 * 06..05  CODE<1..0> Encoding, 00=NRZ
	 * 04..03  DRATE<1..0> DPLL Divisor, 00=8
	 * 02      Reserved, must be 0
	 * 01..00  CNCT<1..0> Channel connection, 0=normal
	 *
	 * 0000 0000
	 */
	RegValue = 0x00;
	switch(info->params.encoding) {
	case HDLC_ENCODING_NRZI:	  RegValue |= BIT5; break;
	case HDLC_ENCODING_BIPHASE_MARK:  RegValue |= BIT7 + BIT5; break; /* aka FM1 */
	case HDLC_ENCODING_BIPHASE_SPACE: RegValue |= BIT7 + BIT6; break; /* aka FM0 */
	case HDLC_ENCODING_BIPHASE_LEVEL: RegValue |= BIT7; break; 	/* aka Manchester */
#if 0
	case HDLC_ENCODING_NRZB:	       				/* not supported */
	case HDLC_ENCODING_NRZI_MARK:          				/* not supported */
	case HDLC_ENCODING_DIFF_BIPHASE_LEVEL: 				/* not supported */
#endif
	}
	if ( info->params.flags & HDLC_FLAG_DPLL_DIV16 ) {
		DpllDivisor = 16;
		RegValue |= BIT3;
	} else if ( info->params.flags & HDLC_FLAG_DPLL_DIV8 ) {
		DpllDivisor = 8;
	} else {
		DpllDivisor = 32;
		RegValue |= BIT4;
	}
	write_reg(info, MD2, RegValue);


	/* RXS, Receive clock source
	 *
	 * 07      Reserved, must be 0
	 * 06..04  RXCS<2..0>, clock source, 000=RxC Pin, 100=BRG, 110=DPLL
	 * 03..00  RXBR<3..0>, rate divisor, 0000=1
	 */
	RegValue=0;
	if (info->params.flags & HDLC_FLAG_RXC_BRG)
		RegValue |= BIT6;
	if (info->params.flags & HDLC_FLAG_RXC_DPLL)
		RegValue |= BIT6 + BIT5;
	write_reg(info, RXS, RegValue);

	/* TXS, Transmit clock source
	 *
	 * 07      Reserved, must be 0
	 * 06..04  RXCS<2..0>, clock source, 000=TxC Pin, 100=BRG, 110=Receive Clock
	 * 03..00  RXBR<3..0>, rate divisor, 0000=1
	 */
	RegValue=0;
	if (info->params.flags & HDLC_FLAG_TXC_BRG)
		RegValue |= BIT6;
	if (info->params.flags & HDLC_FLAG_TXC_DPLL)
		RegValue |= BIT6 + BIT5;
	write_reg(info, TXS, RegValue);

	if (info->params.flags & HDLC_FLAG_RXC_DPLL)
		set_rate(info, info->params.clock_speed * DpllDivisor);
	else
		set_rate(info, info->params.clock_speed);

	/* GPDATA (General Purpose I/O Data Register)
	 *
	 * 6,4,2,0  CLKSEL<3..0>, 0 = TcCLK in, 1 = Auxclk out
	 */
	if (info->params.flags & HDLC_FLAG_TXC_BRG)
		info->port_array[0]->ctrlreg_value |= (BIT0 << (info->port_num * 2));
	else
		info->port_array[0]->ctrlreg_value &= ~(BIT0 << (info->port_num * 2));
	write_control_reg(info);

	/* RRC Receive Ready Control 0
	 *
	 * 07..05  Reserved, must be 0
	 * 04..00  RRC<4..0> Rx FIFO trigger active
	 */
	write_reg(info, RRC, rx_active_fifo_level);

	/* TRC0 Transmit Ready Control 0
	 *
	 * 07..05  Reserved, must be 0
	 * 04..00  TRC<4..0> Tx FIFO trigger active
	 */
	write_reg(info, TRC0, tx_active_fifo_level);

	/* TRC1 Transmit Ready Control 1
	 *
	 * 07..05  Reserved, must be 0
	 * 04..00  TRC<4..0> Tx FIFO trigger inactive 0x1f = 32 bytes (full)
	 */
	write_reg(info, TRC1, (unsigned char)(tx_negate_fifo_level - 1));

	/* DMR, DMA Mode Register
	 *
	 * 07..05  Reserved, must be 0
	 * 04      TMOD, Transfer Mode: 1=chained-block
	 * 03      Reserved, must be 0
	 * 02      NF, Number of Frames: 1=multi-frame
	 * 01      CNTE, Frame End IRQ Counter enable: 0=disabled
	 * 00      Reserved, must be 0
	 *
	 * 0001 0100
	 */
	write_reg(info, TXDMA + DMR, 0x14);
	write_reg(info, RXDMA + DMR, 0x14);

	/* Set chain pointer base (upper 8 bits of 24 bit addr) */
	write_reg(info, RXDMA + CPB,
		(unsigned char)(info->buffer_list_phys >> 16));

	/* Set chain pointer base (upper 8 bits of 24 bit addr) */
	write_reg(info, TXDMA + CPB,
		(unsigned char)(info->buffer_list_phys >> 16));

	/* enable status interrupts. other code enables/disables
	 * the individual sources for these two interrupt classes.
	 */
	info->ie0_value |= TXINTE + RXINTE;
	write_reg(info, IE0, info->ie0_value);

	/* CTL, MSCI control register
	 *
	 * 07..06  Reserved, set to 0
	 * 05      UDRNC, underrun control, 0=abort 1=CRC+flag (HDLC/BSC)
	 * 04      IDLC, idle control, 0=mark 1=idle register
	 * 03      BRK, break, 0=off 1 =on (async)
	 * 02      SYNCLD, sync char load enable (BSC) 1=enabled
	 * 01      GOP, go active on poll (LOOP mode) 1=enabled
	 * 00      RTS, RTS output control, 0=active 1=inactive
	 *
	 * 0001 0001
	 */
	RegValue = 0x10;
	if (!(info->serial_signals & SerialSignal_RTS))
		RegValue |= 0x01;
	write_reg(info, CTL, RegValue);

	/* preamble not supported ! */

	tx_set_idle(info);
	tx_stop(info);
	rx_stop(info);

	set_rate(info, info->params.clock_speed);

	if (info->params.loopback)
		enable_loopback(info,1);
}

/* Set the transmit HDLC idle mode
 */
static void tx_set_idle(SLMP_INFO *info)
{
	unsigned char RegValue = 0xff;

	/* Map API idle mode to SCA register bits */
	switch(info->idle_mode) {
	case HDLC_TXIDLE_FLAGS:			RegValue = 0x7e; break;
	case HDLC_TXIDLE_ALT_ZEROS_ONES:	RegValue = 0xaa; break;
	case HDLC_TXIDLE_ZEROS:			RegValue = 0x00; break;
	case HDLC_TXIDLE_ONES:			RegValue = 0xff; break;
	case HDLC_TXIDLE_ALT_MARK_SPACE:	RegValue = 0xaa; break;
	case HDLC_TXIDLE_SPACE:			RegValue = 0x00; break;
	case HDLC_TXIDLE_MARK:			RegValue = 0xff; break;
	}

	write_reg(info, IDL, RegValue);
}

/* Query the adapter for the state of the V24 status (input) signals.
 */
static void get_signals(SLMP_INFO *info)
{
	u16 status = read_reg(info, SR3);
	u16 gpstatus = read_status_reg(info);
	u16 testbit;

	/* clear all serial signals except DTR and RTS */
	info->serial_signals &= SerialSignal_DTR + SerialSignal_RTS;

	/* set serial signal bits to reflect MISR */

	if (!(status & BIT3))
		info->serial_signals |= SerialSignal_CTS;

	if ( !(status & BIT2))
		info->serial_signals |= SerialSignal_DCD;

	testbit = BIT1 << (info->port_num * 2); // Port 0..3 RI is GPDATA<1,3,5,7>
	if (!(gpstatus & testbit))
		info->serial_signals |= SerialSignal_RI;

	testbit = BIT0 << (info->port_num * 2); // Port 0..3 DSR is GPDATA<0,2,4,6>
	if (!(gpstatus & testbit))
		info->serial_signals |= SerialSignal_DSR;
}

/* Set the state of DTR and RTS based on contents of
 * serial_signals member of device context.
 */
static void set_signals(SLMP_INFO *info)
{
	unsigned char RegValue;
	u16 EnableBit;

	RegValue = read_reg(info, CTL);
	if (info->serial_signals & SerialSignal_RTS)
		RegValue &= ~BIT0;
	else
		RegValue |= BIT0;
	write_reg(info, CTL, RegValue);

	// Port 0..3 DTR is ctrl reg <1,3,5,7>
	EnableBit = BIT1 << (info->port_num*2);
	if (info->serial_signals & SerialSignal_DTR)
		info->port_array[0]->ctrlreg_value &= ~EnableBit;
	else
		info->port_array[0]->ctrlreg_value |= EnableBit;
	write_control_reg(info);
}

/*******************/
/* DMA Buffer Code */
/*******************/

/* Set the count for all receive buffers to SCABUFSIZE
 * and set the current buffer to the first buffer. This effectively
 * makes all buffers free and discards any data in buffers.
 */
static void rx_reset_buffers(SLMP_INFO *info)
{
	rx_free_frame_buffers(info, 0, info->rx_buf_count - 1);
}

/* Free the buffers used by a received frame
 *
 * info   pointer to device instance data
 * first  index of 1st receive buffer of frame
 * last   index of last receive buffer of frame
 */
static void rx_free_frame_buffers(SLMP_INFO *info, unsigned int first, unsigned int last)
{
	bool done = false;

	while(!done) {
	        /* reset current buffer for reuse */
		info->rx_buf_list[first].status = 0xff;

	        if (first == last) {
	                done = true;
	                /* set new last rx descriptor address */
			write_reg16(info, RXDMA + EDA, info->rx_buf_list_ex[first].phys_entry);
	        }

	        first++;
		if (first == info->rx_buf_count)
			first = 0;
	}

	/* set current buffer to next buffer after last buffer of frame */
	info->current_rx_buf = first;
}

/* Return a received frame from the receive DMA buffers.
 * Only frames received without errors are returned.
 *
 * Return Value:	true if frame returned, otherwise false
 */
static bool rx_get_frame(SLMP_INFO *info)
{
	unsigned int StartIndex, EndIndex;	/* index of 1st and last buffers of Rx frame */
	unsigned short status;
	unsigned int framesize = 0;
	bool ReturnCode = false;
	unsigned long flags;
	struct tty_struct *tty = info->port.tty;
	unsigned char addr_field = 0xff;
   	SCADESC *desc;
	SCADESC_EX *desc_ex;

CheckAgain:
	/* assume no frame returned, set zero length */
	framesize = 0;
	addr_field = 0xff;

	/*
	 * current_rx_buf points to the 1st buffer of the next available
	 * receive frame. To find the last buffer of the frame look for
	 * a non-zero status field in the buffer entries. (The status
	 * field is set by the 16C32 after completing a receive frame.
	 */
	StartIndex = EndIndex = info->current_rx_buf;

	for ( ;; ) {
		desc = &info->rx_buf_list[EndIndex];
		desc_ex = &info->rx_buf_list_ex[EndIndex];

		if (desc->status == 0xff)
			goto Cleanup;	/* current desc still in use, no frames available */

		if (framesize == 0 && info->params.addr_filter != 0xff)
			addr_field = desc_ex->virt_addr[0];

		framesize += desc->length;

		/* Status != 0 means last buffer of frame */
		if (desc->status)
			break;

		EndIndex++;
		if (EndIndex == info->rx_buf_count)
			EndIndex = 0;

		if (EndIndex == info->current_rx_buf) {
			/* all buffers have been 'used' but none mark	   */
			/* the end of a frame. Reset buffers and receiver. */
			if ( info->rx_enabled ){
				spin_lock_irqsave(&info->lock,flags);
				rx_start(info);
				spin_unlock_irqrestore(&info->lock,flags);
			}
			goto Cleanup;
		}

	}

	/* check status of receive frame */

	/* frame status is byte stored after frame data
	 *
	 * 7 EOM (end of msg), 1 = last buffer of frame
	 * 6 Short Frame, 1 = short frame
	 * 5 Abort, 1 = frame aborted
	 * 4 Residue, 1 = last byte is partial
	 * 3 Overrun, 1 = overrun occurred during frame reception
	 * 2 CRC,     1 = CRC error detected
	 *
	 */
	status = desc->status;

	/* ignore CRC bit if not using CRC (bit is undefined) */
	/* Note:CRC is not save to data buffer */
	if (info->params.crc_type == HDLC_CRC_NONE)
		status &= ~BIT2;

	if (framesize == 0 ||
		 (addr_field != 0xff && addr_field != info->params.addr_filter)) {
		/* discard 0 byte frames, this seems to occur sometime
		 * when remote is idling flags.
		 */
		rx_free_frame_buffers(info, StartIndex, EndIndex);
		goto CheckAgain;
	}

	if (framesize < 2)
		status |= BIT6;

	if (status & (BIT6+BIT5+BIT3+BIT2)) {
		/* received frame has errors,
		 * update counts and mark frame size as 0
		 */
		if (status & BIT6)
			info->icount.rxshort++;
		else if (status & BIT5)
			info->icount.rxabort++;
		else if (status & BIT3)
			info->icount.rxover++;
		else
			info->icount.rxcrc++;

		framesize = 0;
#if SYNCLINK_GENERIC_HDLC
		{
			info->netdev->stats.rx_errors++;
			info->netdev->stats.rx_frame_errors++;
		}
#endif
	}

	if ( debug_level >= DEBUG_LEVEL_BH )
		printk("%s(%d):%s rx_get_frame() status=%04X size=%d\n",
			__FILE__,__LINE__,info->device_name,status,framesize);

	if ( debug_level >= DEBUG_LEVEL_DATA )
		trace_block(info,info->rx_buf_list_ex[StartIndex].virt_addr,
			min_t(unsigned int, framesize, SCABUFSIZE), 0);

	if (framesize) {
		if (framesize > info->max_frame_size)
			info->icount.rxlong++;
		else {
			/* copy dma buffer(s) to contiguous intermediate buffer */
			int copy_count = framesize;
			int index = StartIndex;
			unsigned char *ptmp = info->tmp_rx_buf;
			info->tmp_rx_buf_count = framesize;

			info->icount.rxok++;

			while(copy_count) {
				int partial_count = min(copy_count,SCABUFSIZE);
				memcpy( ptmp,
					info->rx_buf_list_ex[index].virt_addr,
					partial_count );
				ptmp += partial_count;
				copy_count -= partial_count;

				if ( ++index == info->rx_buf_count )
					index = 0;
			}

#if SYNCLINK_GENERIC_HDLC
			if (info->netcount)
				hdlcdev_rx(info,info->tmp_rx_buf,framesize);
			else
#endif
				ldisc_receive_buf(tty,info->tmp_rx_buf,
						  info->flag_buf, framesize);
		}
	}
	/* Free the buffers used by this frame. */
	rx_free_frame_buffers( info, StartIndex, EndIndex );

	ReturnCode = true;

Cleanup:
	if ( info->rx_enabled && info->rx_overflow ) {
		/* Receiver is enabled, but needs to restarted due to
		 * rx buffer overflow. If buffers are empty, restart receiver.
		 */
		if (info->rx_buf_list[EndIndex].status == 0xff) {
			spin_lock_irqsave(&info->lock,flags);
			rx_start(info);
			spin_unlock_irqrestore(&info->lock,flags);
		}
	}

	return ReturnCode;
}

/* load the transmit DMA buffer with data
 */
static void tx_load_dma_buffer(SLMP_INFO *info, const char *buf, unsigned int count)
{
	unsigned short copy_count;
	unsigned int i = 0;
	SCADESC *desc;
	SCADESC_EX *desc_ex;

	if ( debug_level >= DEBUG_LEVEL_DATA )
		trace_block(info, buf, min_t(unsigned int, count, SCABUFSIZE), 1);

	/* Copy source buffer to one or more DMA buffers, starting with
	 * the first transmit dma buffer.
	 */
	for(i=0;;)
	{
		copy_count = min_t(unsigned int, count, SCABUFSIZE);

		desc = &info->tx_buf_list[i];
		desc_ex = &info->tx_buf_list_ex[i];

		load_pci_memory(info, desc_ex->virt_addr,buf,copy_count);

		desc->length = copy_count;
		desc->status = 0;

		buf += copy_count;
		count -= copy_count;

		if (!count)
			break;

		i++;
		if (i >= info->tx_buf_count)
			i = 0;
	}

	info->tx_buf_list[i].status = 0x81;	/* set EOM and EOT status */
	info->last_tx_buf = ++i;
}

static bool register_test(SLMP_INFO *info)
{
	static unsigned char testval[] = {0x00, 0xff, 0xaa, 0x55, 0x69, 0x96};
	static unsigned int count = ARRAY_SIZE(testval);
	unsigned int i;
	bool rc = true;
	unsigned long flags;

	spin_lock_irqsave(&info->lock,flags);
	reset_port(info);

	/* assume failure */
	info->init_error = DiagStatus_AddressFailure;

	/* Write bit patterns to various registers but do it out of */
	/* sync, then read back and verify values. */

	for (i = 0 ; i < count ; i++) {
		write_reg(info, TMC, testval[i]);
		write_reg(info, IDL, testval[(i+1)%count]);
		write_reg(info, SA0, testval[(i+2)%count]);
		write_reg(info, SA1, testval[(i+3)%count]);

		if ( (read_reg(info, TMC) != testval[i]) ||
			  (read_reg(info, IDL) != testval[(i+1)%count]) ||
			  (read_reg(info, SA0) != testval[(i+2)%count]) ||
			  (read_reg(info, SA1) != testval[(i+3)%count]) )
		{
			rc = false;
			break;
		}
	}

	reset_port(info);
	spin_unlock_irqrestore(&info->lock,flags);

	return rc;
}

static bool irq_test(SLMP_INFO *info)
{
	unsigned long timeout;
	unsigned long flags;

	unsigned char timer = (info->port_num & 1) ? TIMER2 : TIMER0;

	spin_lock_irqsave(&info->lock,flags);
	reset_port(info);

	/* assume failure */
	info->init_error = DiagStatus_IrqFailure;
	info->irq_occurred = false;

	/* setup timer0 on SCA0 to interrupt */

	/* IER2<7..4> = timer<3..0> interrupt enables (1=enabled) */
	write_reg(info, IER2, (unsigned char)((info->port_num & 1) ? BIT6 : BIT4));

	write_reg(info, (unsigned char)(timer + TEPR), 0);	/* timer expand prescale */
	write_reg16(info, (unsigned char)(timer + TCONR), 1);	/* timer constant */


	/* TMCS, Timer Control/Status Register
	 *
	 * 07      CMF, Compare match flag (read only) 1=match
	 * 06      ECMI, CMF Interrupt Enable: 1=enabled
	 * 05      Reserved, must be 0
	 * 04      TME, Timer Enable
	 * 03..00  Reserved, must be 0
	 *
	 * 0101 0000
	 */
	write_reg(info, (unsigned char)(timer + TMCS), 0x50);

	spin_unlock_irqrestore(&info->lock,flags);

	timeout=100;
	while( timeout-- && !info->irq_occurred ) {
		msleep_interruptible(10);
	}

	spin_lock_irqsave(&info->lock,flags);
	reset_port(info);
	spin_unlock_irqrestore(&info->lock,flags);

	return info->irq_occurred;
}

/* initialize individual SCA device (2 ports)
 */
static bool sca_init(SLMP_INFO *info)
{
	/* set wait controller to single mem partition (low), no wait states */
	write_reg(info, PABR0, 0);	/* wait controller addr boundary 0 */
	write_reg(info, PABR1, 0);	/* wait controller addr boundary 1 */
	write_reg(info, WCRL, 0);	/* wait controller low range */
	write_reg(info, WCRM, 0);	/* wait controller mid range */
	write_reg(info, WCRH, 0);	/* wait controller high range */

	/* DPCR, DMA Priority Control
	 *
	 * 07..05  Not used, must be 0
	 * 04      BRC, bus release condition: 0=all transfers complete
	 * 03      CCC, channel change condition: 0=every cycle
	 * 02..00  PR<2..0>, priority 100=round robin
	 *
	 * 00000100 = 0x04
	 */
	write_reg(info, DPCR, dma_priority);

	/* DMA Master Enable, BIT7: 1=enable all channels */
	write_reg(info, DMER, 0x80);

	/* enable all interrupt classes */
	write_reg(info, IER0, 0xff);	/* TxRDY,RxRDY,TxINT,RxINT (ports 0-1) */
	write_reg(info, IER1, 0xff);	/* DMIB,DMIA (channels 0-3) */
	write_reg(info, IER2, 0xf0);	/* TIRQ (timers 0-3) */

	/* ITCR, interrupt control register
	 * 07      IPC, interrupt priority, 0=MSCI->DMA
	 * 06..05  IAK<1..0>, Acknowledge cycle, 00=non-ack cycle
	 * 04      VOS, Vector Output, 0=unmodified vector
	 * 03..00  Reserved, must be 0
	 */
	write_reg(info, ITCR, 0);

	return true;
}

/* initialize adapter hardware
 */
static bool init_adapter(SLMP_INFO *info)
{
	int i;

	/* Set BIT30 of Local Control Reg 0x50 to reset SCA */
	volatile u32 *MiscCtrl = (u32 *)(info->lcr_base + 0x50);
	u32 readval;

	info->misc_ctrl_value |= BIT30;
	*MiscCtrl = info->misc_ctrl_value;

	/*
	 * Force at least 170ns delay before clearing
	 * reset bit. Each read from LCR takes at least
	 * 30ns so 10 times for 300ns to be safe.
	 */
	for(i=0;i<10;i++)
		readval = *MiscCtrl;

	info->misc_ctrl_value &= ~BIT30;
	*MiscCtrl = info->misc_ctrl_value;

	/* init control reg (all DTRs off, all clksel=input) */
	info->ctrlreg_value = 0xaa;
	write_control_reg(info);

	{
		volatile u32 *LCR1BRDR = (u32 *)(info->lcr_base + 0x2c);
		lcr1_brdr_value &= ~(BIT5 + BIT4 + BIT3);

		switch(read_ahead_count)
		{
		case 16:
			lcr1_brdr_value |= BIT5 + BIT4 + BIT3;
			break;
		case 8:
			lcr1_brdr_value |= BIT5 + BIT4;
			break;
		case 4:
			lcr1_brdr_value |= BIT5 + BIT3;
			break;
		case 0:
			lcr1_brdr_value |= BIT5;
			break;
		}

		*LCR1BRDR = lcr1_brdr_value;
		*MiscCtrl = misc_ctrl_value;
	}

	sca_init(info->port_array[0]);
	sca_init(info->port_array[2]);

	return true;
}

/* Loopback an HDLC frame to test the hardware
 * interrupt and DMA functions.
 */
static bool loopback_test(SLMP_INFO *info)
{
#define TESTFRAMESIZE 20

	unsigned long timeout;
	u16 count = TESTFRAMESIZE;
	unsigned char buf[TESTFRAMESIZE];
	bool rc = false;
	unsigned long flags;

	struct tty_struct *oldtty = info->port.tty;
	u32 speed = info->params.clock_speed;

	info->params.clock_speed = 3686400;
	info->port.tty = NULL;

	/* assume failure */
	info->init_error = DiagStatus_DmaFailure;

	/* build and send transmit frame */
	for (count = 0; count < TESTFRAMESIZE;++count)
		buf[count] = (unsigned char)count;

	memset(info->tmp_rx_buf,0,TESTFRAMESIZE);

	/* program hardware for HDLC and enabled receiver */
	spin_lock_irqsave(&info->lock,flags);
	hdlc_mode(info);
	enable_loopback(info,1);
       	rx_start(info);
	info->tx_count = count;
	tx_load_dma_buffer(info,buf,count);
	tx_start(info);
	spin_unlock_irqrestore(&info->lock,flags);

	/* wait for receive complete */
	/* Set a timeout for waiting for interrupt. */
	for ( timeout = 100; timeout; --timeout ) {
		msleep_interruptible(10);

		if (rx_get_frame(info)) {
			rc = true;
			break;
		}
	}

	/* verify received frame length and contents */
	if (rc &&
	    ( info->tmp_rx_buf_count != count ||
	      memcmp(buf, info->tmp_rx_buf,count))) {
		rc = false;
	}

	spin_lock_irqsave(&info->lock,flags);
	reset_adapter(info);
	spin_unlock_irqrestore(&info->lock,flags);

	info->params.clock_speed = speed;
	info->port.tty = oldtty;

	return rc;
}

/* Perform diagnostics on hardware
 */
static int adapter_test( SLMP_INFO *info )
{
	unsigned long flags;
	if ( debug_level >= DEBUG_LEVEL_INFO )
		printk( "%s(%d):Testing device %s\n",
			__FILE__,__LINE__,info->device_name );

	spin_lock_irqsave(&info->lock,flags);
	init_adapter(info);
	spin_unlock_irqrestore(&info->lock,flags);

	info->port_array[0]->port_count = 0;

	if ( register_test(info->port_array[0]) &&
		register_test(info->port_array[1])) {

		info->port_array[0]->port_count = 2;

		if ( register_test(info->port_array[2]) &&
			register_test(info->port_array[3]) )
			info->port_array[0]->port_count += 2;
	}
	else {
		printk( "%s(%d):Register test failure for device %s Addr=%08lX\n",
			__FILE__,__LINE__,info->device_name, (unsigned long)(info->phys_sca_base));
		return -ENODEV;
	}

	if ( !irq_test(info->port_array[0]) ||
		!irq_test(info->port_array[1]) ||
		 (info->port_count == 4 && !irq_test(info->port_array[2])) ||
		 (info->port_count == 4 && !irq_test(info->port_array[3]))) {
		printk( "%s(%d):Interrupt test failure for device %s IRQ=%d\n",
			__FILE__,__LINE__,info->device_name, (unsigned short)(info->irq_level) );
		return -ENODEV;
	}

	if (!loopback_test(info->port_array[0]) ||
		!loopback_test(info->port_array[1]) ||
		 (info->port_count == 4 && !loopback_test(info->port_array[2])) ||
		 (info->port_count == 4 && !loopback_test(info->port_array[3]))) {
		printk( "%s(%d):DMA test failure for device %s\n",
			__FILE__,__LINE__,info->device_name);
		return -ENODEV;
	}

	if ( debug_level >= DEBUG_LEVEL_INFO )
		printk( "%s(%d):device %s passed diagnostics\n",
			__FILE__,__LINE__,info->device_name );

	info->port_array[0]->init_error = 0;
	info->port_array[1]->init_error = 0;
	if ( info->port_count > 2 ) {
		info->port_array[2]->init_error = 0;
		info->port_array[3]->init_error = 0;
	}

	return 0;
}

/* Test the shared memory on a PCI adapter.
 */
static bool memory_test(SLMP_INFO *info)
{
	static unsigned long testval[] = { 0x0, 0x55555555, 0xaaaaaaaa,
		0x66666666, 0x99999999, 0xffffffff, 0x12345678 };
	unsigned long count = ARRAY_SIZE(testval);
	unsigned long i;
	unsigned long limit = SCA_MEM_SIZE/sizeof(unsigned long);
	unsigned long * addr = (unsigned long *)info->memory_base;

	/* Test data lines with test pattern at one location. */

	for ( i = 0 ; i < count ; i++ ) {
		*addr = testval[i];
		if ( *addr != testval[i] )
			return false;
	}

	/* Test address lines with incrementing pattern over */
	/* entire address range. */

	for ( i = 0 ; i < limit ; i++ ) {
		*addr = i * 4;
		addr++;
	}

	addr = (unsigned long *)info->memory_base;

	for ( i = 0 ; i < limit ; i++ ) {
		if ( *addr != i * 4 )
			return false;
		addr++;
	}

	memset( info->memory_base, 0, SCA_MEM_SIZE );
	return true;
}

/* Load data into PCI adapter shared memory.
 *
 * The PCI9050 releases control of the local bus
 * after completing the current read or write operation.
 *
 * While the PCI9050 write FIFO not empty, the
 * PCI9050 treats all of the writes as a single transaction
 * and does not release the bus. This causes DMA latency problems
 * at high speeds when copying large data blocks to the shared memory.
 *
 * This function breaks a write into multiple transations by
 * interleaving a read which flushes the write FIFO and 'completes'
 * the write transation. This allows any pending DMA request to gain control
 * of the local bus in a timely fasion.
 */
static void load_pci_memory(SLMP_INFO *info, char* dest, const char* src, unsigned short count)
{
	/* A load interval of 16 allows for 4 32-bit writes at */
	/* 136ns each for a maximum latency of 542ns on the local bus.*/

	unsigned short interval = count / sca_pci_load_interval;
	unsigned short i;

	for ( i = 0 ; i < interval ; i++ )
	{
		memcpy(dest, src, sca_pci_load_interval);
		read_status_reg(info);
		dest += sca_pci_load_interval;
		src += sca_pci_load_interval;
	}

	memcpy(dest, src, count % sca_pci_load_interval);
}

static void trace_block(SLMP_INFO *info,const char* data, int count, int xmit)
{
	int i;
	int linecount;
	if (xmit)
		printk("%s tx data:\n",info->device_name);
	else
		printk("%s rx data:\n",info->device_name);

	while(count) {
		if (count > 16)
			linecount = 16;
		else
			linecount = count;

		for(i=0;i<linecount;i++)
			printk("%02X ",(unsigned char)data[i]);
		for(;i<17;i++)
			printk("   ");
		for(i=0;i<linecount;i++) {
			if (data[i]>=040 && data[i]<=0176)
				printk("%c",data[i]);
			else
				printk(".");
		}
		printk("\n");

		data  += linecount;
		count -= linecount;
	}
}	/* end of trace_block() */

/* called when HDLC frame times out
 * update stats and do tx completion processing
 */
static void tx_timeout(unsigned long context)
{
	SLMP_INFO *info = (SLMP_INFO*)context;
	unsigned long flags;

	if ( debug_level >= DEBUG_LEVEL_INFO )
		printk( "%s(%d):%s tx_timeout()\n",
			__FILE__,__LINE__,info->device_name);
	if(info->tx_active && info->params.mode == MGSL_MODE_HDLC) {
		info->icount.txtimeout++;
	}
	spin_lock_irqsave(&info->lock,flags);
	info->tx_active = false;
	info->tx_count = info->tx_put = info->tx_get = 0;

	spin_unlock_irqrestore(&info->lock,flags);

#if SYNCLINK_GENERIC_HDLC
	if (info->netcount)
		hdlcdev_tx_done(info);
	else
#endif
		bh_transmit(info);
}

/* called to periodically check the DSR/RI modem signal input status
 */
static void status_timeout(unsigned long context)
{
	u16 status = 0;
	SLMP_INFO *info = (SLMP_INFO*)context;
	unsigned long flags;
	unsigned char delta;


	spin_lock_irqsave(&info->lock,flags);
	get_signals(info);
	spin_unlock_irqrestore(&info->lock,flags);

	/* check for DSR/RI state change */

	delta = info->old_signals ^ info->serial_signals;
	info->old_signals = info->serial_signals;

	if (delta & SerialSignal_DSR)
		status |= MISCSTATUS_DSR_LATCHED|(info->serial_signals&SerialSignal_DSR);

	if (delta & SerialSignal_RI)
		status |= MISCSTATUS_RI_LATCHED|(info->serial_signals&SerialSignal_RI);

	if (delta & SerialSignal_DCD)
		status |= MISCSTATUS_DCD_LATCHED|(info->serial_signals&SerialSignal_DCD);

	if (delta & SerialSignal_CTS)
		status |= MISCSTATUS_CTS_LATCHED|(info->serial_signals&SerialSignal_CTS);

	if (status)
		isr_io_pin(info,status);

	mod_timer(&info->status_timer, jiffies + msecs_to_jiffies(10));
}


/* Register Access Routines -
 * All registers are memory mapped
 */
#define CALC_REGADDR() \
	unsigned char * RegAddr = (unsigned char*)(info->sca_base + Addr); \
	if (info->port_num > 1) \
		RegAddr += 256;	    		/* port 0-1 SCA0, 2-3 SCA1 */ \
	if ( info->port_num & 1) { \
		if (Addr > 0x7f) \
			RegAddr += 0x40;	/* DMA access */ \
		else if (Addr > 0x1f && Addr < 0x60) \
			RegAddr += 0x20;	/* MSCI access */ \
	}


static unsigned char read_reg(SLMP_INFO * info, unsigned char Addr)
{
	CALC_REGADDR();
	return *RegAddr;
}
static void write_reg(SLMP_INFO * info, unsigned char Addr, unsigned char Value)
{
	CALC_REGADDR();
	*RegAddr = Value;
}

static u16 read_reg16(SLMP_INFO * info, unsigned char Addr)
{
	CALC_REGADDR();
	return *((u16 *)RegAddr);
}

static void write_reg16(SLMP_INFO * info, unsigned char Addr, u16 Value)
{
	CALC_REGADDR();
	*((u16 *)RegAddr) = Value;
}

static unsigned char read_status_reg(SLMP_INFO * info)
{
	unsigned char *RegAddr = (unsigned char *)info->statctrl_base;
	return *RegAddr;
}

static void write_control_reg(SLMP_INFO * info)
{
	unsigned char *RegAddr = (unsigned char *)info->statctrl_base;
	*RegAddr = info->port_array[0]->ctrlreg_value;
}


static int __devinit synclinkmp_init_one (struct pci_dev *dev,
					  const struct pci_device_id *ent)
{
	if (pci_enable_device(dev)) {
		printk("error enabling pci device %p\n", dev);
		return -EIO;
	}
	device_init( ++synclinkmp_adapter_count, dev );
	return 0;
}

static void __devexit synclinkmp_remove_one (struct pci_dev *dev)
{
}
