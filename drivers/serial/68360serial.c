/*
 *  UART driver for 68360 CPM SCC or SMC
 *  Copyright (c) 2000 D. Jeff Dionne <jeff@uclinux.org>,
 *  Copyright (c) 2000 Michael Leslie <mleslie@lineo.ca>
 *  Copyright (c) 1997 Dan Malek <dmalek@jlc.net>
 *
 * I used the serial.c driver as the framework for this driver.
 * Give credit to those guys.
 * The original code was written for the MBX860 board.  I tried to make
 * it generic, but there may be some assumptions in the structures that
 * have to be fixed later.
 * To save porting time, I did not bother to change any object names
 * that are not accessed outside of this file.
 * It still needs lots of work........When it was easy, I included code
 * to support the SCCs, but this has never been tested, nor is it complete.
 * Only the SCCs support modem control, so that is not complete either.
 *
 * This module exports the following rs232 io functions:
 *
 *	int rs_360_init(void);
 */

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial.h>
#include <linux/serialP.h> 
#include <linux/major.h>
#include <linux/string.h>
#include <linux/fcntl.h>
#include <linux/ptrace.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <asm/irq.h>
#include <asm/m68360.h>
#include <asm/commproc.h>

 
#ifdef CONFIG_KGDB
extern void breakpoint(void);
extern void set_debug_traps(void);
extern int  kgdb_output_string (const char* s, unsigned int count);
#endif


/* #ifdef CONFIG_SERIAL_CONSOLE */ /* This seems to be a post 2.0 thing - mles */
#include <linux/console.h>
#include <linux/jiffies.h>

/* this defines the index into rs_table for the port to use
 */
#ifndef CONFIG_SERIAL_CONSOLE_PORT
#define CONFIG_SERIAL_CONSOLE_PORT	1 /* ie SMC2 - note USE_SMC2 must be defined */
#endif
/* #endif */

#if 0
/* SCC2 for console
 */
#undef CONFIG_SERIAL_CONSOLE_PORT
#define CONFIG_SERIAL_CONSOLE_PORT	2
#endif


#define TX_WAKEUP	ASYNC_SHARE_IRQ

static char *serial_name = "CPM UART driver";
static char *serial_version = "0.03";

static struct tty_driver *serial_driver;
int serial_console_setup(struct console *co, char *options);

/*
 * Serial driver configuration section.  Here are the various options:
 */
#define SERIAL_PARANOIA_CHECK
#define CONFIG_SERIAL_NOPAUSE_IO
#define SERIAL_DO_RESTART

/* Set of debugging defines */

#undef SERIAL_DEBUG_INTR
#undef SERIAL_DEBUG_OPEN
#undef SERIAL_DEBUG_FLOW
#undef SERIAL_DEBUG_RS_WAIT_UNTIL_SENT

#define _INLINE_ inline
  
#define DBG_CNT(s)

/* We overload some of the items in the data structure to meet our
 * needs.  For example, the port address is the CPM parameter ram
 * offset for the SCC or SMC.  The maximum number of ports is 4 SCCs and
 * 2 SMCs.  The "hub6" field is used to indicate the channel number, with
 * a flag indicating SCC or SMC, and the number is used as an index into
 * the CPM parameter area for this device.
 * The "type" field is currently set to 0, for PORT_UNKNOWN.  It is
 * not currently used.  I should probably use it to indicate the port
 * type of SMC or SCC.
 * The SMCs do not support any modem control signals.
 */
#define smc_scc_num	hub6
#define NUM_IS_SCC	((int)0x00010000)
#define PORT_NUM(P)	((P) & 0x0000ffff)


#if defined (CONFIG_UCQUICC)

volatile extern void *_periph_base;
/* sipex transceiver
 *   mode bits for       are on pins
 *
 *    SCC2                d16..19
 *    SCC3                d20..23
 *    SCC4                d24..27
 */
#define SIPEX_MODE(n,m) ((m & 0x0f)<<(16+4*(n-1)))

static uint sipex_mode_bits = 0x00000000;

#endif

/* There is no `serial_state' defined back here in 2.0.
 * Try to get by with serial_struct
 */
/* #define serial_state serial_struct */

/* 2.4 -> 2.0 portability problem: async_icount in 2.4 has a few
 * extras: */

#if 0
struct async_icount_24 {
	__u32   cts, dsr, rng, dcd, tx, rx;
	__u32   frame, parity, overrun, brk;
	__u32   buf_overrun;
} icount;
#endif

#if 0

struct serial_state {
        int     magic;
        int     baud_base;
        unsigned long   port;
        int     irq;
        int     flags;
        int     hub6;
        int     type;
        int     line;
        int     revision;       /* Chip revision (950) */
        int     xmit_fifo_size;
        int     custom_divisor;
        int     count;
        u8      *iomem_base;
        u16     iomem_reg_shift;
        unsigned short  close_delay;
        unsigned short  closing_wait; /* time to wait before closing */
        struct async_icount_24     icount; 
        int     io_type;
        struct async_struct *info;
};
#endif

#define SSTATE_MAGIC 0x5302



/* SMC2 is sometimes used for low performance TDM interfaces.  Define
 * this as 1 if you want SMC2 as a serial port UART managed by this driver.
 * Define this as 0 if you wish to use SMC2 for something else.
 */
#define USE_SMC2 1

#if 0
/* Define SCC to ttySx mapping. */
#define SCC_NUM_BASE	(USE_SMC2 + 1)	/* SCC base tty "number" */

/* Define which SCC is the first one to use for a serial port.  These
 * are 0-based numbers, i.e. this assumes the first SCC (SCC1) is used
 * for Ethernet, and the first available SCC for serial UART is SCC2.
 * NOTE:  IF YOU CHANGE THIS, you have to change the PROFF_xxx and
 * interrupt vectors in the table below to match.
 */
#define SCC_IDX_BASE	1	/* table index */
#endif


/* Processors other than the 860 only get SMCs configured by default.
 * Either they don't have SCCs or they are allocated somewhere else.
 * Of course, there are now 860s without some SCCs, so we will need to
 * address that someday.
 * The Embedded Planet Multimedia I/O cards use TDM interfaces to the
 * stereo codec parts, and we use SMC2 to help support that.
 */
static struct serial_state rs_table[] = {
/*  type   line   PORT           IRQ       FLAGS  smc_scc_num (F.K.A. hub6) */
	{  0,     0, PRSLOT_SMC1, CPMVEC_SMC1,   0,    0 }    /* SMC1 ttyS0 */
#if USE_SMC2
	,{ 0,     0, PRSLOT_SMC2, CPMVEC_SMC2,   0,    1 }     /* SMC2 ttyS1 */
#endif

#if defined(CONFIG_SERIAL_68360_SCC)
	,{ 0,     0, PRSLOT_SCC2, CPMVEC_SCC2,   0, (NUM_IS_SCC | 1) }    /* SCC2 ttyS2 */
	,{ 0,     0, PRSLOT_SCC3, CPMVEC_SCC3,   0, (NUM_IS_SCC | 2) }    /* SCC3 ttyS3 */
	,{ 0,     0, PRSLOT_SCC4, CPMVEC_SCC4,   0, (NUM_IS_SCC | 3) }    /* SCC4 ttyS4 */
#endif
};

#define NR_PORTS	(sizeof(rs_table)/sizeof(struct serial_state))

/* The number of buffer descriptors and their sizes.
 */
#define RX_NUM_FIFO	4
#define RX_BUF_SIZE	32
#define TX_NUM_FIFO	4
#define TX_BUF_SIZE	32

#define CONSOLE_NUM_FIFO 2
#define CONSOLE_BUF_SIZE 4

char *console_fifos[CONSOLE_NUM_FIFO * CONSOLE_BUF_SIZE];

/* The async_struct in serial.h does not really give us what we
 * need, so define our own here.
 */
typedef struct serial_info {
	int			magic;
	int			flags;

	struct serial_state	*state;
 	/* struct serial_struct	*state; */
 	/* struct async_struct	*state; */
	
	struct tty_struct 	*tty;
	int			read_status_mask;
	int			ignore_status_mask;
	int			timeout;
	int			line;
	int			x_char;	/* xon/xoff character */
	int			close_delay;
	unsigned short		closing_wait;
	unsigned short		closing_wait2;
	unsigned long		event;
	unsigned long		last_active;
	int			blocked_open; /* # of blocked opens */
	struct work_struct	tqueue;
	struct work_struct	tqueue_hangup;
 	wait_queue_head_t	open_wait; 
 	wait_queue_head_t	close_wait; 

	
/* CPM Buffer Descriptor pointers.
	*/
	QUICC_BD			*rx_bd_base;
	QUICC_BD			*rx_cur;
	QUICC_BD			*tx_bd_base;
	QUICC_BD			*tx_cur;
} ser_info_t;


/* since kmalloc_init() does not get called until much after this initialization: */
static ser_info_t  quicc_ser_info[NR_PORTS];
static char rx_buf_pool[NR_PORTS * RX_NUM_FIFO * RX_BUF_SIZE];
static char tx_buf_pool[NR_PORTS * TX_NUM_FIFO * TX_BUF_SIZE];

static void change_speed(ser_info_t *info);
static void rs_360_wait_until_sent(struct tty_struct *tty, int timeout);

static inline int serial_paranoia_check(ser_info_t *info,
					char *name, const char *routine)
{
#ifdef SERIAL_PARANOIA_CHECK
	static const char *badmagic =
		"Warning: bad magic number for serial struct (%s) in %s\n";
	static const char *badinfo =
		"Warning: null async_struct for (%s) in %s\n";

	if (!info) {
		printk(badinfo, name, routine);
		return 1;
	}
	if (info->magic != SERIAL_MAGIC) {
		printk(badmagic, name, routine);
		return 1;
	}
#endif
	return 0;
}

/*
 * This is used to figure out the divisor speeds and the timeouts,
 * indexed by the termio value.  The generic CPM functions are responsible
 * for setting and assigning baud rate generators for us.
 */
static int baud_table[] = {
	0, 50, 75, 110, 134, 150, 200, 300, 600, 1200, 1800, 2400, 4800,
	9600, 19200, 38400, 57600, 115200, 230400, 460800, 0 };

/* This sucks. There is a better way: */
#if defined(CONFIG_CONSOLE_9600)
  #define CONSOLE_BAUDRATE 9600
#elif defined(CONFIG_CONSOLE_19200)
  #define CONSOLE_BAUDRATE 19200
#elif defined(CONFIG_CONSOLE_115200)
  #define CONSOLE_BAUDRATE 115200
#else
  #warning "console baud rate undefined"
  #define CONSOLE_BAUDRATE 9600
#endif

/*
 * ------------------------------------------------------------
 * rs_stop() and rs_start()
 *
 * This routines are called before setting or resetting tty->stopped.
 * They enable or disable transmitter interrupts, as necessary.
 * ------------------------------------------------------------
 */
static void rs_360_stop(struct tty_struct *tty)
{
	ser_info_t *info = (ser_info_t *)tty->driver_data;
	int	idx;
	unsigned long flags;
 	volatile struct scc_regs *sccp;
 	volatile struct smc_regs *smcp;

	if (serial_paranoia_check(info, tty->name, "rs_stop"))
		return;
	
	local_irq_save(flags);
	idx = PORT_NUM(info->state->smc_scc_num);
	if (info->state->smc_scc_num & NUM_IS_SCC) {
		sccp = &pquicc->scc_regs[idx];
		sccp->scc_sccm &= ~UART_SCCM_TX;
	} else {
		/* smcp = &cpmp->cp_smc[idx]; */
		smcp = &pquicc->smc_regs[idx];
		smcp->smc_smcm &= ~SMCM_TX;
	}
	local_irq_restore(flags);
}


static void rs_360_start(struct tty_struct *tty)
{
	ser_info_t *info = (ser_info_t *)tty->driver_data;
	int	idx;
	unsigned long flags;
	volatile struct scc_regs *sccp;
	volatile struct smc_regs *smcp;

	if (serial_paranoia_check(info, tty->name, "rs_stop"))
		return;
	
	local_irq_save(flags);
	idx = PORT_NUM(info->state->smc_scc_num);
	if (info->state->smc_scc_num & NUM_IS_SCC) {
		sccp = &pquicc->scc_regs[idx];
		sccp->scc_sccm |= UART_SCCM_TX;
	} else {
		smcp = &pquicc->smc_regs[idx];
		smcp->smc_smcm |= SMCM_TX;
	}
	local_irq_restore(flags);
}

/*
 * ----------------------------------------------------------------------
 *
 * Here starts the interrupt handling routines.  All of the following
 * subroutines are declared as inline and are folded into
 * rs_interrupt().  They were separated out for readability's sake.
 *
 * Note: rs_interrupt() is a "fast" interrupt, which means that it
 * runs with interrupts turned off.  People who may want to modify
 * rs_interrupt() should try to keep the interrupt handler as fast as
 * possible.  After you are done making modifications, it is not a bad
 * idea to do:
 * 
 * gcc -S -DKERNEL -Wall -Wstrict-prototypes -O6 -fomit-frame-pointer serial.c
 *
 * and look at the resulting assemble code in serial.s.
 *
 * 				- Ted Ts'o (tytso@mit.edu), 7-Mar-93
 * -----------------------------------------------------------------------
 */

static _INLINE_ void receive_chars(ser_info_t *info)
{
	struct tty_struct *tty = info->tty;
	unsigned char ch, flag, *cp;
	/*int	ignored = 0;*/
	int	i;
	ushort	status;
	 struct	async_icount *icount; 
	/* struct	async_icount_24 *icount; */
	volatile QUICC_BD	*bdp;

	icount = &info->state->icount;

	/* Just loop through the closed BDs and copy the characters into
	 * the buffer.
	 */
	bdp = info->rx_cur;
	for (;;) {
		if (bdp->status & BD_SC_EMPTY)	/* If this one is empty */
			break;			/*   we are all done */

		/* The read status mask tell us what we should do with
		 * incoming characters, especially if errors occur.
		 * One special case is the use of BD_SC_EMPTY.  If
		 * this is not set, we are supposed to be ignoring
		 * inputs.  In this case, just mark the buffer empty and
		 * continue.
		 */
		if (!(info->read_status_mask & BD_SC_EMPTY)) {
			bdp->status |= BD_SC_EMPTY;
			bdp->status &=
				~(BD_SC_BR | BD_SC_FR | BD_SC_PR | BD_SC_OV);

			if (bdp->status & BD_SC_WRAP)
				bdp = info->rx_bd_base;
			else
				bdp++;
			continue;
		}

		/* Get the number of characters and the buffer pointer.
		*/
		i = bdp->length;
		/* cp = (unsigned char *)__va(bdp->buf); */
		cp = (char *)bdp->buf;
		status = bdp->status;

		while (i-- > 0) {
			ch = *cp++;
			icount->rx++;

#ifdef SERIAL_DEBUG_INTR
			printk("DR%02x:%02x...", ch, status);
#endif
			flag = TTY_NORMAL;

			if (status & (BD_SC_BR | BD_SC_FR |
				       BD_SC_PR | BD_SC_OV)) {
				/*
				 * For statistics only
				 */
				if (status & BD_SC_BR)
					icount->brk++;
				else if (status & BD_SC_PR)
					icount->parity++;
				else if (status & BD_SC_FR)
					icount->frame++;
				if (status & BD_SC_OV)
					icount->overrun++;

				/*
				 * Now check to see if character should be
				 * ignored, and mask off conditions which
				 * should be ignored.
				if (status & info->ignore_status_mask) {
					if (++ignored > 100)
						break;
					continue;
				}
				 */
				status &= info->read_status_mask;
		
				if (status & (BD_SC_BR)) {
#ifdef SERIAL_DEBUG_INTR
					printk("handling break....");
#endif
					*tty->flip.flag_buf_ptr = TTY_BREAK;
					if (info->flags & ASYNC_SAK)
						do_SAK(tty);
				} else if (status & BD_SC_PR)
					flag = TTY_PARITY;
				else if (status & BD_SC_FR)
					flag = TTY_FRAME;
			}
			tty_insert_flip_char(tty, ch, flag);
			if (status & BD_SC_OV)
				/*
				 * Overrun is special, since it's
				 * reported immediately, and doesn't
				 * affect the current character
				 */
				tty_insert_flip_char(tty, 0, TTY_OVERRUN);
		}

		/* This BD is ready to be used again.  Clear status.
		 * Get next BD.
		 */
		bdp->status |= BD_SC_EMPTY;
		bdp->status &= ~(BD_SC_BR | BD_SC_FR | BD_SC_PR | BD_SC_OV);

		if (bdp->status & BD_SC_WRAP)
			bdp = info->rx_bd_base;
		else
			bdp++;
	}

	info->rx_cur = (QUICC_BD *)bdp;

	tty_schedule_flip(tty);
}

static _INLINE_ void receive_break(ser_info_t *info)
{
	struct tty_struct *tty = info->tty;

	info->state->icount.brk++;
	/* Check to see if there is room in the tty buffer for
	 * the break.  If not, we exit now, losing the break.  FIXME
	 */
	tty_insert_flip_char(tty, 0, TTY_BREAK);
	tty_schedule_flip(tty);
}

static _INLINE_ void transmit_chars(ser_info_t *info)
{

	if ((info->flags & TX_WAKEUP) ||
	    (info->tty->flags & (1 << TTY_DO_WRITE_WAKEUP))) {
		schedule_work(&info->tqueue);
	}

#ifdef SERIAL_DEBUG_INTR
	printk("THRE...");
#endif
}

#ifdef notdef
	/* I need to do this for the SCCs, so it is left as a reminder.
	*/
static _INLINE_ void check_modem_status(struct async_struct *info)
{
	int	status;
	/* struct	async_icount *icount; */
	struct	async_icount_24 *icount;
	
	status = serial_in(info, UART_MSR);

	if (status & UART_MSR_ANY_DELTA) {
		icount = &info->state->icount;
		/* update input line counters */
		if (status & UART_MSR_TERI)
			icount->rng++;
		if (status & UART_MSR_DDSR)
			icount->dsr++;
		if (status & UART_MSR_DDCD) {
			icount->dcd++;
#ifdef CONFIG_HARD_PPS
			if ((info->flags & ASYNC_HARDPPS_CD) &&
			    (status & UART_MSR_DCD))
				hardpps();
#endif
		}
		if (status & UART_MSR_DCTS)
			icount->cts++;
		wake_up_interruptible(&info->delta_msr_wait);
	}

	if ((info->flags & ASYNC_CHECK_CD) && (status & UART_MSR_DDCD)) {
#if (defined(SERIAL_DEBUG_OPEN) || defined(SERIAL_DEBUG_INTR))
		printk("ttys%d CD now %s...", info->line,
		       (status & UART_MSR_DCD) ? "on" : "off");
#endif		
		if (status & UART_MSR_DCD)
			wake_up_interruptible(&info->open_wait);
		else {
#ifdef SERIAL_DEBUG_OPEN
			printk("scheduling hangup...");
#endif
			queue_task(&info->tqueue_hangup,
					   &tq_scheduler);
		}
	}
	if (info->flags & ASYNC_CTS_FLOW) {
		if (info->tty->hw_stopped) {
			if (status & UART_MSR_CTS) {
#if (defined(SERIAL_DEBUG_INTR) || defined(SERIAL_DEBUG_FLOW))
				printk("CTS tx start...");
#endif
				info->tty->hw_stopped = 0;
				info->IER |= UART_IER_THRI;
				serial_out(info, UART_IER, info->IER);
				rs_sched_event(info, RS_EVENT_WRITE_WAKEUP);
				return;
			}
		} else {
			if (!(status & UART_MSR_CTS)) {
#if (defined(SERIAL_DEBUG_INTR) || defined(SERIAL_DEBUG_FLOW))
				printk("CTS tx stop...");
#endif
				info->tty->hw_stopped = 1;
				info->IER &= ~UART_IER_THRI;
				serial_out(info, UART_IER, info->IER);
			}
		}
	}
}
#endif

/*
 * This is the serial driver's interrupt routine for a single port
 */
/* static void rs_360_interrupt(void *dev_id) */ /* until and if we start servicing irqs here */
static void rs_360_interrupt(int vec, void *dev_id)
{
	u_char	events;
	int	idx;
	ser_info_t *info;
	volatile struct smc_regs *smcp;
	volatile struct scc_regs *sccp;
	
	info = dev_id;

	idx = PORT_NUM(info->state->smc_scc_num);
	if (info->state->smc_scc_num & NUM_IS_SCC) {
		sccp = &pquicc->scc_regs[idx];
		events = sccp->scc_scce;
		if (events & SCCM_RX)
			receive_chars(info);
		if (events & SCCM_TX)
			transmit_chars(info);
		sccp->scc_scce = events;
	} else {
		smcp = &pquicc->smc_regs[idx];
		events = smcp->smc_smce;
		if (events & SMCM_BRKE)
			receive_break(info);
		if (events & SMCM_RX)
			receive_chars(info);
		if (events & SMCM_TX)
			transmit_chars(info);
		smcp->smc_smce = events;
	}
	
#ifdef SERIAL_DEBUG_INTR
	printk("rs_interrupt_single(%d, %x)...",
					info->state->smc_scc_num, events);
#endif
#ifdef modem_control
	check_modem_status(info);
#endif
	info->last_active = jiffies;
#ifdef SERIAL_DEBUG_INTR
	printk("end.\n");
#endif
}


/*
 * -------------------------------------------------------------------
 * Here ends the serial interrupt routines.
 * -------------------------------------------------------------------
 */


static void do_softint(void *private_)
{
	ser_info_t	*info = (ser_info_t *) private_;
	struct tty_struct	*tty;
	
	tty = info->tty;
	if (!tty)
		return;

	if (test_and_clear_bit(RS_EVENT_WRITE_WAKEUP, &info->event))
		tty_wakeup(tty);
}


/*
 * This routine is called from the scheduler tqueue when the interrupt
 * routine has signalled that a hangup has occurred.  The path of
 * hangup processing is:
 *
 * 	serial interrupt routine -> (scheduler tqueue) ->
 * 	do_serial_hangup() -> tty->hangup() -> rs_hangup()
 * 
 */
static void do_serial_hangup(void *private_)
{
	struct async_struct	*info = (struct async_struct *) private_;
	struct tty_struct	*tty;
	
	tty = info->tty;
	if (!tty)
		return;

	tty_hangup(tty);
}


static int startup(ser_info_t *info)
{
	unsigned long flags;
	int	retval=0;
	int	idx;
	/*struct serial_state *state = info->state;*/
	volatile struct smc_regs *smcp;
	volatile struct scc_regs *sccp;
	volatile struct smc_uart_pram	*up;
	volatile struct uart_pram	    *scup;


	local_irq_save(flags);

	if (info->flags & ASYNC_INITIALIZED) {
		goto errout;
	}

#ifdef maybe
	if (!state->port || !state->type) {
		if (info->tty)
			set_bit(TTY_IO_ERROR, &info->tty->flags);
		goto errout;
	}
#endif

#ifdef SERIAL_DEBUG_OPEN
	printk("starting up ttys%d (irq %d)...", info->line, state->irq);
#endif


#ifdef modem_control
	info->MCR = 0;
	if (info->tty->termios->c_cflag & CBAUD)
		info->MCR = UART_MCR_DTR | UART_MCR_RTS;
#endif
	
	if (info->tty)
		clear_bit(TTY_IO_ERROR, &info->tty->flags);

	/*
	 * and set the speed of the serial port
	 */
	change_speed(info);

	idx = PORT_NUM(info->state->smc_scc_num);
	if (info->state->smc_scc_num & NUM_IS_SCC) {
		sccp = &pquicc->scc_regs[idx];
		scup = &pquicc->pram[info->state->port].scc.pscc.u;

		scup->mrblr = RX_BUF_SIZE;
		scup->max_idl = RX_BUF_SIZE;

		sccp->scc_sccm |= (UART_SCCM_TX | UART_SCCM_RX);
		sccp->scc_gsmr.w.low |= (SCC_GSMRL_ENR | SCC_GSMRL_ENT);

	} else {
		smcp = &pquicc->smc_regs[idx];

		/* Enable interrupts and I/O.
		*/
		smcp->smc_smcm |= (SMCM_RX | SMCM_TX);
		smcp->smc_smcmr |= (SMCMR_REN | SMCMR_TEN);

		/* We can tune the buffer length and idle characters
		 * to take advantage of the entire incoming buffer size.
		 * If mrblr is something other than 1, maxidl has to be
		 * non-zero or we never get an interrupt.  The maxidl
		 * is the number of character times we wait after reception
		 * of the last character before we decide no more characters
		 * are coming.
		 */
		/* up = (smc_uart_t *)&pquicc->cp_dparam[state->port]; */
		/* holy unionized structures, Batman: */
		up = &pquicc->pram[info->state->port].scc.pothers.idma_smc.psmc.u;

		up->mrblr = RX_BUF_SIZE;
		up->max_idl = RX_BUF_SIZE;

		up->brkcr = 1;	/* number of break chars */
	}

	info->flags |= ASYNC_INITIALIZED;
	local_irq_restore(flags);
	return 0;
	
errout:
	local_irq_restore(flags);
	return retval;
}

/*
 * This routine will shutdown a serial port; interrupts are disabled, and
 * DTR is dropped if the hangup on close termio flag is on.
 */
static void shutdown(ser_info_t *info)
{
	unsigned long	flags;
	struct serial_state *state;
	int		idx;
	volatile struct smc_regs	*smcp;
	volatile struct scc_regs	*sccp;

	if (!(info->flags & ASYNC_INITIALIZED))
		return;

	state = info->state;

#ifdef SERIAL_DEBUG_OPEN
	printk("Shutting down serial port %d (irq %d)....", info->line,
	       state->irq);
#endif
	
	local_irq_save(flags);

	idx = PORT_NUM(state->smc_scc_num);
	if (state->smc_scc_num & NUM_IS_SCC) {
		sccp = &pquicc->scc_regs[idx];
		sccp->scc_gsmr.w.low &= ~(SCC_GSMRL_ENR | SCC_GSMRL_ENT);
#ifdef CONFIG_SERIAL_CONSOLE
		/* We can't disable the transmitter if this is the
		 * system console.
		 */
		if ((state - rs_table) != CONFIG_SERIAL_CONSOLE_PORT)
#endif
		sccp->scc_sccm &= ~(UART_SCCM_TX | UART_SCCM_RX);
	} else {
		smcp = &pquicc->smc_regs[idx];

		/* Disable interrupts and I/O.
		 */
		smcp->smc_smcm &= ~(SMCM_RX | SMCM_TX);
#ifdef CONFIG_SERIAL_CONSOLE
		/* We can't disable the transmitter if this is the
		 * system console.
		 */
		if ((state - rs_table) != CONFIG_SERIAL_CONSOLE_PORT)
#endif
			smcp->smc_smcmr &= ~(SMCMR_REN | SMCMR_TEN);
	}
	
	if (info->tty)
		set_bit(TTY_IO_ERROR, &info->tty->flags);

	info->flags &= ~ASYNC_INITIALIZED;
	local_irq_restore(flags);
}

/*
 * This routine is called to set the UART divisor registers to match
 * the specified baud rate for a serial port.
 */
static void change_speed(ser_info_t *info)
{
	int	baud_rate;
	unsigned cflag, cval, scval, prev_mode;
	int	i, bits, sbits, idx;
	unsigned long	flags;
	struct serial_state *state;
	volatile struct smc_regs	*smcp;
	volatile struct scc_regs	*sccp;

	if (!info->tty || !info->tty->termios)
		return;
	cflag = info->tty->termios->c_cflag;

	state = info->state;

	/* Character length programmed into the mode register is the
	 * sum of: 1 start bit, number of data bits, 0 or 1 parity bit,
	 * 1 or 2 stop bits, minus 1.
	 * The value 'bits' counts this for us.
	 */
	cval = 0;
	scval = 0;

	/* byte size and parity */
	switch (cflag & CSIZE) {
	      case CS5: bits = 5; break;
	      case CS6: bits = 6; break;
	      case CS7: bits = 7; break;
	      case CS8: bits = 8; break;
	      /* Never happens, but GCC is too dumb to figure it out */
	      default:  bits = 8; break;
	}
	sbits = bits - 5;

	if (cflag & CSTOPB) {
		cval |= SMCMR_SL;	/* Two stops */
		scval |= SCU_PMSR_SL;
		bits++;
	}
	if (cflag & PARENB) {
		cval |= SMCMR_PEN;
		scval |= SCU_PMSR_PEN;
		bits++;
	}
	if (!(cflag & PARODD)) {
		cval |= SMCMR_PM_EVEN;
		scval |= (SCU_PMSR_REVP | SCU_PMSR_TEVP);
	}

	/* Determine divisor based on baud rate */
	i = cflag & CBAUD;
	if (i >= (sizeof(baud_table)/sizeof(int)))
		baud_rate = 9600;
	else
		baud_rate = baud_table[i];

	info->timeout = (TX_BUF_SIZE*HZ*bits);
	info->timeout += HZ/50;		/* Add .02 seconds of slop */

#ifdef modem_control
	/* CTS flow control flag and modem status interrupts */
	info->IER &= ~UART_IER_MSI;
	if (info->flags & ASYNC_HARDPPS_CD)
		info->IER |= UART_IER_MSI;
	if (cflag & CRTSCTS) {
		info->flags |= ASYNC_CTS_FLOW;
		info->IER |= UART_IER_MSI;
	} else
		info->flags &= ~ASYNC_CTS_FLOW;
	if (cflag & CLOCAL)
		info->flags &= ~ASYNC_CHECK_CD;
	else {
		info->flags |= ASYNC_CHECK_CD;
		info->IER |= UART_IER_MSI;
	}
	serial_out(info, UART_IER, info->IER);
#endif

	/*
	 * Set up parity check flag
	 */
	info->read_status_mask = (BD_SC_EMPTY | BD_SC_OV);
	if (I_INPCK(info->tty))
		info->read_status_mask |= BD_SC_FR | BD_SC_PR;
	if (I_BRKINT(info->tty) || I_PARMRK(info->tty))
		info->read_status_mask |= BD_SC_BR;
	
	/*
	 * Characters to ignore
	 */
	info->ignore_status_mask = 0;
	if (I_IGNPAR(info->tty))
		info->ignore_status_mask |= BD_SC_PR | BD_SC_FR;
	if (I_IGNBRK(info->tty)) {
		info->ignore_status_mask |= BD_SC_BR;
		/*
		 * If we're ignore parity and break indicators, ignore 
		 * overruns too.  (For real raw support).
		 */
		if (I_IGNPAR(info->tty))
			info->ignore_status_mask |= BD_SC_OV;
	}
	/*
	 * !!! ignore all characters if CREAD is not set
	 */
	if ((cflag & CREAD) == 0)
	 info->read_status_mask &= ~BD_SC_EMPTY;
	 local_irq_save(flags);

	 /* Start bit has not been added (so don't, because we would just
	  * subtract it later), and we need to add one for the number of
	  * stops bits (there is always at least one).
	  */
	 bits++;
	 idx = PORT_NUM(state->smc_scc_num);
	 if (state->smc_scc_num & NUM_IS_SCC) {
         sccp = &pquicc->scc_regs[idx];
         sccp->scc_psmr = (sbits << 12) | scval;
     } else {
         smcp = &pquicc->smc_regs[idx];

		/* Set the mode register.  We want to keep a copy of the
		 * enables, because we want to put them back if they were
		 * present.
		 */
		prev_mode = smcp->smc_smcmr;
		smcp->smc_smcmr = smcr_mk_clen(bits) | cval |  SMCMR_SM_UART;
		smcp->smc_smcmr |= (prev_mode & (SMCMR_REN | SMCMR_TEN));
	}

	m360_cpm_setbrg((state - rs_table), baud_rate);

	local_irq_restore(flags);
}

static void rs_360_put_char(struct tty_struct *tty, unsigned char ch)
{
	ser_info_t *info = (ser_info_t *)tty->driver_data;
	volatile QUICC_BD	*bdp;

	if (serial_paranoia_check(info, tty->name, "rs_put_char"))
		return;

	if (!tty)
		return;

	bdp = info->tx_cur;
	while (bdp->status & BD_SC_READY);

	/* *((char *)__va(bdp->buf)) = ch; */
	*((char *)bdp->buf) = ch;
	bdp->length = 1;
	bdp->status |= BD_SC_READY;

	/* Get next BD.
	*/
	if (bdp->status & BD_SC_WRAP)
		bdp = info->tx_bd_base;
	else
		bdp++;

	info->tx_cur = (QUICC_BD *)bdp;

}

static int rs_360_write(struct tty_struct * tty,
		    const unsigned char *buf, int count)
{
	int	c, ret = 0;
	ser_info_t *info = (ser_info_t *)tty->driver_data;
	volatile QUICC_BD *bdp;

#ifdef CONFIG_KGDB
	/* Try to let stub handle output. Returns true if it did. */ 
	if (kgdb_output_string(buf, count))
		return ret;
#endif

	if (serial_paranoia_check(info, tty->name, "rs_write"))
		return 0;

	if (!tty) 
		return 0;

	bdp = info->tx_cur;

	while (1) {
		c = min(count, TX_BUF_SIZE);

		if (c <= 0)
			break;

		if (bdp->status & BD_SC_READY) {
			info->flags |= TX_WAKEUP;
			break;
		}

		/* memcpy(__va(bdp->buf), buf, c); */
		memcpy((void *)bdp->buf, buf, c);

		bdp->length = c;
		bdp->status |= BD_SC_READY;

		buf += c;
		count -= c;
		ret += c;

		/* Get next BD.
		*/
		if (bdp->status & BD_SC_WRAP)
			bdp = info->tx_bd_base;
		else
			bdp++;
		info->tx_cur = (QUICC_BD *)bdp;
	}
	return ret;
}

static int rs_360_write_room(struct tty_struct *tty)
{
	ser_info_t *info = (ser_info_t *)tty->driver_data;
	int	ret;

	if (serial_paranoia_check(info, tty->name, "rs_write_room"))
		return 0;

	if ((info->tx_cur->status & BD_SC_READY) == 0) {
		info->flags &= ~TX_WAKEUP;
		ret = TX_BUF_SIZE;
	}
	else {
		info->flags |= TX_WAKEUP;
		ret = 0;
	}
	return ret;
}

/* I could track this with transmit counters....maybe later.
*/
static int rs_360_chars_in_buffer(struct tty_struct *tty)
{
	ser_info_t *info = (ser_info_t *)tty->driver_data;
				
	if (serial_paranoia_check(info, tty->name, "rs_chars_in_buffer"))
		return 0;
	return 0;
}

static void rs_360_flush_buffer(struct tty_struct *tty)
{
	ser_info_t *info = (ser_info_t *)tty->driver_data;
				
	if (serial_paranoia_check(info, tty->name, "rs_flush_buffer"))
		return;

	/* There is nothing to "flush", whatever we gave the CPM
	 * is on its way out.
	 */
	tty_wakeup(tty);
	info->flags &= ~TX_WAKEUP;
}

/*
 * This function is used to send a high-priority XON/XOFF character to
 * the device
 */
static void rs_360_send_xchar(struct tty_struct *tty, char ch)
{
	volatile QUICC_BD	*bdp;

	ser_info_t *info = (ser_info_t *)tty->driver_data;

	if (serial_paranoia_check(info, tty->name, "rs_send_char"))
		return;

	bdp = info->tx_cur;
	while (bdp->status & BD_SC_READY);

	/* *((char *)__va(bdp->buf)) = ch; */
	*((char *)bdp->buf) = ch;
	bdp->length = 1;
	bdp->status |= BD_SC_READY;

	/* Get next BD.
	*/
	if (bdp->status & BD_SC_WRAP)
		bdp = info->tx_bd_base;
	else
		bdp++;

	info->tx_cur = (QUICC_BD *)bdp;
}

/*
 * ------------------------------------------------------------
 * rs_throttle()
 * 
 * This routine is called by the upper-layer tty layer to signal that
 * incoming characters should be throttled.
 * ------------------------------------------------------------
 */
static void rs_360_throttle(struct tty_struct * tty)
{
	ser_info_t *info = (ser_info_t *)tty->driver_data;
#ifdef SERIAL_DEBUG_THROTTLE
	char	buf[64];
	
	printk("throttle %s: %d....\n", _tty_name(tty, buf),
	       tty->ldisc.chars_in_buffer(tty));
#endif

	if (serial_paranoia_check(info, tty->name, "rs_throttle"))
		return;
	
	if (I_IXOFF(tty))
		rs_360_send_xchar(tty, STOP_CHAR(tty));

#ifdef modem_control
	if (tty->termios->c_cflag & CRTSCTS)
		info->MCR &= ~UART_MCR_RTS;

	local_irq_disable();
	serial_out(info, UART_MCR, info->MCR);
	local_irq_enable();
#endif
}

static void rs_360_unthrottle(struct tty_struct * tty)
{
	ser_info_t *info = (ser_info_t *)tty->driver_data;
#ifdef SERIAL_DEBUG_THROTTLE
	char	buf[64];
	
	printk("unthrottle %s: %d....\n", _tty_name(tty, buf),
	       tty->ldisc.chars_in_buffer(tty));
#endif

	if (serial_paranoia_check(info, tty->name, "rs_unthrottle"))
		return;
	
	if (I_IXOFF(tty)) {
		if (info->x_char)
			info->x_char = 0;
		else
			rs_360_send_xchar(tty, START_CHAR(tty));
	}
#ifdef modem_control
	if (tty->termios->c_cflag & CRTSCTS)
		info->MCR |= UART_MCR_RTS;
	local_irq_disable();
	serial_out(info, UART_MCR, info->MCR);
	local_irq_enable();
#endif
}

/*
 * ------------------------------------------------------------
 * rs_ioctl() and friends
 * ------------------------------------------------------------
 */

#ifdef maybe
/*
 * get_lsr_info - get line status register info
 *
 * Purpose: Let user call ioctl() to get info when the UART physically
 * 	    is emptied.  On bus types like RS485, the transmitter must
 * 	    release the bus after transmitting. This must be done when
 * 	    the transmit shift register is empty, not be done when the
 * 	    transmit holding register is empty.  This functionality
 * 	    allows an RS485 driver to be written in user space. 
 */
static int get_lsr_info(struct async_struct * info, unsigned int *value)
{
	unsigned char status;
	unsigned int result;

	local_irq_disable();
	status = serial_in(info, UART_LSR);
	local_irq_enable();
	result = ((status & UART_LSR_TEMT) ? TIOCSER_TEMT : 0);
	return put_user(result,value);
}
#endif

static int rs_360_tiocmget(struct tty_struct *tty, struct file *file)
{
	ser_info_t *info = (ser_info_t *)tty->driver_data;
	unsigned int result = 0;
#ifdef modem_control
	unsigned char control, status;

	if (serial_paranoia_check(info, tty->name, __FUNCTION__))
		return -ENODEV;

	if (tty->flags & (1 << TTY_IO_ERROR))
		return -EIO;

	control = info->MCR;
	local_irq_disable();
	status = serial_in(info, UART_MSR);
	local_irq_enable();
	result =  ((control & UART_MCR_RTS) ? TIOCM_RTS : 0)
		| ((control & UART_MCR_DTR) ? TIOCM_DTR : 0)
#ifdef TIOCM_OUT1
		| ((control & UART_MCR_OUT1) ? TIOCM_OUT1 : 0)
		| ((control & UART_MCR_OUT2) ? TIOCM_OUT2 : 0)
#endif
		| ((status  & UART_MSR_DCD) ? TIOCM_CAR : 0)
		| ((status  & UART_MSR_RI) ? TIOCM_RNG : 0)
		| ((status  & UART_MSR_DSR) ? TIOCM_DSR : 0)
		| ((status  & UART_MSR_CTS) ? TIOCM_CTS : 0);
#endif
	return result;
}

static int rs_360_tiocmset(struct tty_struct *tty, struct file *file,
			   unsigned int set, unsigned int clear)
{
#ifdef modem_control
	ser_info_t *info = (ser_info_t *)tty->driver_data;
 	unsigned int arg;

	if (serial_paranoia_check(info, tty->name, __FUNCTION__))
		return -ENODEV;

	if (tty->flags & (1 << TTY_IO_ERROR))
		return -EIO;
	/* FIXME: locking on info->mcr */
 	if (set & TIOCM_RTS)
 		info->mcr |= UART_MCR_RTS;
 	if (set & TIOCM_DTR)
 		info->mcr |= UART_MCR_DTR;
	if (clear & TIOCM_RTS)
		info->MCR &= ~UART_MCR_RTS;
	if (clear & TIOCM_DTR)
		info->MCR &= ~UART_MCR_DTR;

#ifdef TIOCM_OUT1
	if (set & TIOCM_OUT1)
		info->MCR |= UART_MCR_OUT1;
	if (set & TIOCM_OUT2)
		info->MCR |= UART_MCR_OUT2;
	if (clear & TIOCM_OUT1)
		info->MCR &= ~UART_MCR_OUT1;
	if (clear & TIOCM_OUT2)
		info->MCR &= ~UART_MCR_OUT2;
#endif

	local_irq_disable();
	serial_out(info, UART_MCR, info->MCR);
	local_irq_enable();
#endif
	return 0;
}

/* Sending a break is a two step process on the SMC/SCC.  It is accomplished
 * by sending a STOP TRANSMIT command followed by a RESTART TRANSMIT
 * command.  We take advantage of the begin/end functions to make this
 * happen.
 */
static ushort	smc_chan_map[] = {
	CPM_CR_CH_SMC1,
	CPM_CR_CH_SMC2
};

static ushort	scc_chan_map[] = {
	CPM_CR_CH_SCC1,
	CPM_CR_CH_SCC2,
	CPM_CR_CH_SCC3,
	CPM_CR_CH_SCC4
};

static void begin_break(ser_info_t *info)
{
	volatile QUICC *cp;
	ushort	chan;
	int     idx;

	cp = pquicc;

	idx = PORT_NUM(info->state->smc_scc_num);
	if (info->state->smc_scc_num & NUM_IS_SCC)
		chan = scc_chan_map[idx];
	else
		chan = smc_chan_map[idx];

	cp->cp_cr = mk_cr_cmd(chan, CPM_CR_STOP_TX) | CPM_CR_FLG;
	while (cp->cp_cr & CPM_CR_FLG);
}

static void end_break(ser_info_t *info)
{
	volatile QUICC *cp;
	ushort	chan;
	int idx;

	cp = pquicc;

	idx = PORT_NUM(info->state->smc_scc_num);
	if (info->state->smc_scc_num & NUM_IS_SCC)
		chan = scc_chan_map[idx];
	else
		chan = smc_chan_map[idx];

	cp->cp_cr = mk_cr_cmd(chan, CPM_CR_RESTART_TX) | CPM_CR_FLG;
	while (cp->cp_cr & CPM_CR_FLG);
}

/*
 * This routine sends a break character out the serial port.
 */
static void send_break(ser_info_t *info, unsigned int duration)
{
#ifdef SERIAL_DEBUG_SEND_BREAK
	printk("rs_send_break(%d) jiff=%lu...", duration, jiffies);
#endif
	begin_break(info);
	msleep_interruptible(duration);
	end_break(info);
#ifdef SERIAL_DEBUG_SEND_BREAK
	printk("done jiffies=%lu\n", jiffies);
#endif
}


static int rs_360_ioctl(struct tty_struct *tty, struct file * file,
		    unsigned int cmd, unsigned long arg)
{
	int error;
	ser_info_t *info = (ser_info_t *)tty->driver_data;
	int retval;
	struct async_icount cnow; 
	/* struct async_icount_24 cnow;*/ 	/* kernel counter temps */
	struct serial_icounter_struct *p_cuser;	/* user space */

	if (serial_paranoia_check(info, tty->name, "rs_ioctl"))
		return -ENODEV;

	if ((cmd != TIOCMIWAIT) && (cmd != TIOCGICOUNT)) {
		if (tty->flags & (1 << TTY_IO_ERROR))
		    return -EIO;
	}
	
	switch (cmd) {
		case TCSBRK:	/* SVID version: non-zero arg --> no break */
			retval = tty_check_change(tty);
			if (retval)
				return retval;
			tty_wait_until_sent(tty, 0);
			if (signal_pending(current))
				return -EINTR;
			if (!arg) {
				send_break(info, 250);	/* 1/4 second */
				if (signal_pending(current))
					return -EINTR;
			}
			return 0;
		case TCSBRKP:	/* support for POSIX tcsendbreak() */
			retval = tty_check_change(tty);
			if (retval)
				return retval;
			tty_wait_until_sent(tty, 0);
			if (signal_pending(current))
				return -EINTR;
			send_break(info, arg ? arg*100 : 250);
			if (signal_pending(current))
				return -EINTR;
			return 0;
		case TIOCSBRK:
			retval = tty_check_change(tty);
			if (retval)
				return retval;
			tty_wait_until_sent(tty, 0);
			begin_break(info);
			return 0;
		case TIOCCBRK:
			retval = tty_check_change(tty);
			if (retval)
				return retval;
			end_break(info);
			return 0;
		case TIOCGSOFTCAR:
			/* return put_user(C_CLOCAL(tty) ? 1 : 0, (int *) arg); */
			put_user(C_CLOCAL(tty) ? 1 : 0, (int *) arg);
			return 0;
		case TIOCSSOFTCAR:
			error = get_user(arg, (unsigned int *) arg); 
			if (error)
				return error;
			tty->termios->c_cflag =
				((tty->termios->c_cflag & ~CLOCAL) |
				 (arg ? CLOCAL : 0));
			return 0;
#ifdef maybe
		case TIOCSERGETLSR: /* Get line status register */
			return get_lsr_info(info, (unsigned int *) arg);
#endif
		/*
		 * Wait for any of the 4 modem inputs (DCD,RI,DSR,CTS) to change
		 * - mask passed in arg for lines of interest
 		 *   (use |'ed TIOCM_RNG/DSR/CD/CTS for masking)
		 * Caller should use TIOCGICOUNT to see which one it was
		 */
		 case TIOCMIWAIT:
#ifdef modem_control
			local_irq_disable();
			/* note the counters on entry */
			cprev = info->state->icount;
			local_irq_enable();
			while (1) {
				interruptible_sleep_on(&info->delta_msr_wait);
				/* see if a signal did it */
				if (signal_pending(current))
					return -ERESTARTSYS;
				local_irq_disable();
				cnow = info->state->icount; /* atomic copy */
				local_irq_enable();
				if (cnow.rng == cprev.rng && cnow.dsr == cprev.dsr && 
				    cnow.dcd == cprev.dcd && cnow.cts == cprev.cts)
					return -EIO; /* no change => error */
				if ( ((arg & TIOCM_RNG) && (cnow.rng != cprev.rng)) ||
				     ((arg & TIOCM_DSR) && (cnow.dsr != cprev.dsr)) ||
				     ((arg & TIOCM_CD)  && (cnow.dcd != cprev.dcd)) ||
				     ((arg & TIOCM_CTS) && (cnow.cts != cprev.cts)) ) {
					return 0;
				}
				cprev = cnow;
			}
			/* NOTREACHED */
#else
			return 0;
#endif

		/* 
		 * Get counter of input serial line interrupts (DCD,RI,DSR,CTS)
		 * Return: write counters to the user passed counter struct
		 * NB: both 1->0 and 0->1 transitions are counted except for
		 *     RI where only 0->1 is counted.
		 */
		case TIOCGICOUNT:
			local_irq_disable();
			cnow = info->state->icount;
			local_irq_enable();
			p_cuser = (struct serial_icounter_struct *) arg;
/* 			error = put_user(cnow.cts, &p_cuser->cts); */
/* 			if (error) return error; */
/* 			error = put_user(cnow.dsr, &p_cuser->dsr); */
/* 			if (error) return error; */
/* 			error = put_user(cnow.rng, &p_cuser->rng); */
/* 			if (error) return error; */
/* 			error = put_user(cnow.dcd, &p_cuser->dcd); */
/* 			if (error) return error; */

			put_user(cnow.cts, &p_cuser->cts);
			put_user(cnow.dsr, &p_cuser->dsr);
			put_user(cnow.rng, &p_cuser->rng);
			put_user(cnow.dcd, &p_cuser->dcd);
			return 0;

		default:
			return -ENOIOCTLCMD;
		}
	return 0;
}

/* FIX UP modem control here someday......
*/
static void rs_360_set_termios(struct tty_struct *tty, struct ktermios *old_termios)
{
	ser_info_t *info = (ser_info_t *)tty->driver_data;

	change_speed(info);

#ifdef modem_control
	/* Handle transition to B0 status */
	if ((old_termios->c_cflag & CBAUD) &&
	    !(tty->termios->c_cflag & CBAUD)) {
		info->MCR &= ~(UART_MCR_DTR|UART_MCR_RTS);
		local_irq_disable();
		serial_out(info, UART_MCR, info->MCR);
		local_irq_enable();
	}
	
	/* Handle transition away from B0 status */
	if (!(old_termios->c_cflag & CBAUD) &&
	    (tty->termios->c_cflag & CBAUD)) {
		info->MCR |= UART_MCR_DTR;
		if (!tty->hw_stopped ||
		    !(tty->termios->c_cflag & CRTSCTS)) {
			info->MCR |= UART_MCR_RTS;
		}
		local_irq_disable();
		serial_out(info, UART_MCR, info->MCR);
		local_irq_enable();
	}
	
	/* Handle turning off CRTSCTS */
	if ((old_termios->c_cflag & CRTSCTS) &&
	    !(tty->termios->c_cflag & CRTSCTS)) {
		tty->hw_stopped = 0;
		rs_360_start(tty);
	}
#endif

#if 0
	/*
	 * No need to wake up processes in open wait, since they
	 * sample the CLOCAL flag once, and don't recheck it.
	 * XXX  It's not clear whether the current behavior is correct
	 * or not.  Hence, this may change.....
	 */
	if (!(old_termios->c_cflag & CLOCAL) &&
	    (tty->termios->c_cflag & CLOCAL))
		wake_up_interruptible(&info->open_wait);
#endif
}

/*
 * ------------------------------------------------------------
 * rs_close()
 * 
 * This routine is called when the serial port gets closed.  First, we
 * wait for the last remaining data to be sent.  Then, we unlink its
 * async structure from the interrupt chain if necessary, and we free
 * that IRQ if nothing is left in the chain.
 * ------------------------------------------------------------
 */
static void rs_360_close(struct tty_struct *tty, struct file * filp)
{
	ser_info_t *info = (ser_info_t *)tty->driver_data;
	/* struct async_state *state; */
	struct serial_state *state;
	unsigned long	flags;
	int		idx;
	volatile struct smc_regs	*smcp;
	volatile struct scc_regs	*sccp;

	if (!info || serial_paranoia_check(info, tty->name, "rs_close"))
		return;

	state = info->state;
	
	local_irq_save(flags);
	
	if (tty_hung_up_p(filp)) {
		DBG_CNT("before DEC-hung");
		local_irq_restore(flags);
		return;
	}
	
#ifdef SERIAL_DEBUG_OPEN
	printk("rs_close ttys%d, count = %d\n", info->line, state->count);
#endif
	if ((tty->count == 1) && (state->count != 1)) {
		/*
		 * Uh, oh.  tty->count is 1, which means that the tty
		 * structure will be freed.  state->count should always
		 * be one in these conditions.  If it's greater than
		 * one, we've got real problems, since it means the
		 * serial port won't be shutdown.
		 */
		printk("rs_close: bad serial port count; tty->count is 1, "
		       "state->count is %d\n", state->count);
		state->count = 1;
	}
	if (--state->count < 0) {
		printk("rs_close: bad serial port count for ttys%d: %d\n",
		       info->line, state->count);
		state->count = 0;
	}
	if (state->count) {
		DBG_CNT("before DEC-2");
		local_irq_restore(flags);
		return;
	}
	info->flags |= ASYNC_CLOSING;
	/*
	 * Now we wait for the transmit buffer to clear; and we notify 
	 * the line discipline to only process XON/XOFF characters.
	 */
	tty->closing = 1;
	if (info->closing_wait != ASYNC_CLOSING_WAIT_NONE)
		tty_wait_until_sent(tty, info->closing_wait);
	/*
	 * At this point we stop accepting input.  To do this, we
	 * disable the receive line status interrupts, and tell the
	 * interrupt driver to stop checking the data ready bit in the
	 * line status register.
	 */
	info->read_status_mask &= ~BD_SC_EMPTY;
	if (info->flags & ASYNC_INITIALIZED) {

		idx = PORT_NUM(info->state->smc_scc_num);
		if (info->state->smc_scc_num & NUM_IS_SCC) {
			sccp = &pquicc->scc_regs[idx];
			sccp->scc_sccm &= ~UART_SCCM_RX;
			sccp->scc_gsmr.w.low &= ~SCC_GSMRL_ENR;
		} else {
			smcp = &pquicc->smc_regs[idx];
			smcp->smc_smcm &= ~SMCM_RX;
			smcp->smc_smcmr &= ~SMCMR_REN;
		}
		/*
		 * Before we drop DTR, make sure the UART transmitter
		 * has completely drained; this is especially
		 * important if there is a transmit FIFO!
		 */
		rs_360_wait_until_sent(tty, info->timeout);
	}
	shutdown(info);
	if (tty->driver->flush_buffer)
		tty->driver->flush_buffer(tty);
	tty_ldisc_flush(tty);		
	tty->closing = 0;
	info->event = 0;
	info->tty = 0;
	if (info->blocked_open) {
		if (info->close_delay) {
			msleep_interruptible(jiffies_to_msecs(info->close_delay));
		}
		wake_up_interruptible(&info->open_wait);
	}
	info->flags &= ~(ASYNC_NORMAL_ACTIVE|ASYNC_CLOSING);
	wake_up_interruptible(&info->close_wait);
	local_irq_restore(flags);
}

/*
 * rs_wait_until_sent() --- wait until the transmitter is empty
 */
static void rs_360_wait_until_sent(struct tty_struct *tty, int timeout)
{
	ser_info_t *info = (ser_info_t *)tty->driver_data;
	unsigned long orig_jiffies, char_time;
	/*int lsr;*/
	volatile QUICC_BD *bdp;
	
	if (serial_paranoia_check(info, tty->name, "rs_wait_until_sent"))
		return;

#ifdef maybe
	if (info->state->type == PORT_UNKNOWN)
		return;
#endif

	orig_jiffies = jiffies;
	/*
	 * Set the check interval to be 1/5 of the estimated time to
	 * send a single character, and make it at least 1.  The check
	 * interval should also be less than the timeout.
	 * 
	 * Note: we have to use pretty tight timings here to satisfy
	 * the NIST-PCTS.
	 */
	char_time = 1;
	if (timeout)
		char_time = min(char_time, (unsigned long)timeout);
#ifdef SERIAL_DEBUG_RS_WAIT_UNTIL_SENT
	printk("In rs_wait_until_sent(%d) check=%lu...", timeout, char_time);
	printk("jiff=%lu...", jiffies);
#endif

	/* We go through the loop at least once because we can't tell
	 * exactly when the last character exits the shifter.  There can
	 * be at least two characters waiting to be sent after the buffers
	 * are empty.
	 */
	do {
#ifdef SERIAL_DEBUG_RS_WAIT_UNTIL_SENT
		printk("lsr = %d (jiff=%lu)...", lsr, jiffies);
#endif
/*		current->counter = 0;	 make us low-priority */
		msleep_interruptible(jiffies_to_msecs(char_time));
		if (signal_pending(current))
			break;
		if (timeout && (time_after(jiffies, orig_jiffies + timeout)))
			break;
		/* The 'tx_cur' is really the next buffer to send.  We
		 * have to back up to the previous BD and wait for it
		 * to go.  This isn't perfect, because all this indicates
		 * is the buffer is available.  There are still characters
		 * in the CPM FIFO.
		 */
		bdp = info->tx_cur;
		if (bdp == info->tx_bd_base)
			bdp += (TX_NUM_FIFO-1);
		else
			bdp--;
	} while (bdp->status & BD_SC_READY);
	current->state = TASK_RUNNING;
#ifdef SERIAL_DEBUG_RS_WAIT_UNTIL_SENT
	printk("lsr = %d (jiff=%lu)...done\n", lsr, jiffies);
#endif
}

/*
 * rs_hangup() --- called by tty_hangup() when a hangup is signaled.
 */
static void rs_360_hangup(struct tty_struct *tty)
{
	ser_info_t *info = (ser_info_t *)tty->driver_data;
	struct serial_state *state = info->state;
	
	if (serial_paranoia_check(info, tty->name, "rs_hangup"))
		return;

	state = info->state;
	
	rs_360_flush_buffer(tty);
	shutdown(info);
	info->event = 0;
	state->count = 0;
	info->flags &= ~ASYNC_NORMAL_ACTIVE;
	info->tty = 0;
	wake_up_interruptible(&info->open_wait);
}

/*
 * ------------------------------------------------------------
 * rs_open() and friends
 * ------------------------------------------------------------
 */
static int block_til_ready(struct tty_struct *tty, struct file * filp,
			   ser_info_t *info)
{
#ifdef DO_THIS_LATER
	DECLARE_WAITQUEUE(wait, current);
#endif
	struct serial_state *state = info->state;
	int		retval;
	int		do_clocal = 0;

	/*
	 * If the device is in the middle of being closed, then block
	 * until it's done, and then try again.
	 */
	if (tty_hung_up_p(filp) ||
	    (info->flags & ASYNC_CLOSING)) {
		if (info->flags & ASYNC_CLOSING)
			interruptible_sleep_on(&info->close_wait);
#ifdef SERIAL_DO_RESTART
		if (info->flags & ASYNC_HUP_NOTIFY)
			return -EAGAIN;
		else
			return -ERESTARTSYS;
#else
		return -EAGAIN;
#endif
	}

	/*
	 * If non-blocking mode is set, or the port is not enabled,
	 * then make the check up front and then exit.
	 * If this is an SMC port, we don't have modem control to wait
	 * for, so just get out here.
	 */
	if ((filp->f_flags & O_NONBLOCK) ||
	    (tty->flags & (1 << TTY_IO_ERROR)) ||
	    !(info->state->smc_scc_num & NUM_IS_SCC)) {
		info->flags |= ASYNC_NORMAL_ACTIVE;
		return 0;
	}

	if (tty->termios->c_cflag & CLOCAL)
		do_clocal = 1;
	
	/*
	 * Block waiting for the carrier detect and the line to become
	 * free (i.e., not in use by the callout).  While we are in
	 * this loop, state->count is dropped by one, so that
	 * rs_close() knows when to free things.  We restore it upon
	 * exit, either normal or abnormal.
	 */
	retval = 0;
#ifdef DO_THIS_LATER
	add_wait_queue(&info->open_wait, &wait);
#ifdef SERIAL_DEBUG_OPEN
	printk("block_til_ready before block: ttys%d, count = %d\n",
	       state->line, state->count);
#endif
	local_irq_disable();
	if (!tty_hung_up_p(filp)) 
		state->count--;
	local_irq_enable();
	info->blocked_open++;
	while (1) {
		local_irq_disable();
		if (tty->termios->c_cflag & CBAUD)
			serial_out(info, UART_MCR,
				   serial_inp(info, UART_MCR) |
				   (UART_MCR_DTR | UART_MCR_RTS));
		local_irq_enable();
		set_current_state(TASK_INTERRUPTIBLE);
		if (tty_hung_up_p(filp) ||
		    !(info->flags & ASYNC_INITIALIZED)) {
#ifdef SERIAL_DO_RESTART
			if (info->flags & ASYNC_HUP_NOTIFY)
				retval = -EAGAIN;
			else
				retval = -ERESTARTSYS;	
#else
			retval = -EAGAIN;
#endif
			break;
		}
		if (!(info->flags & ASYNC_CLOSING) &&
		    (do_clocal || (serial_in(info, UART_MSR) &
				   UART_MSR_DCD)))
			break;
		if (signal_pending(current)) {
			retval = -ERESTARTSYS;
			break;
		}
#ifdef SERIAL_DEBUG_OPEN
		printk("block_til_ready blocking: ttys%d, count = %d\n",
		       info->line, state->count);
#endif
		schedule();
	}
	current->state = TASK_RUNNING;
	remove_wait_queue(&info->open_wait, &wait);
	if (!tty_hung_up_p(filp))
		state->count++;
	info->blocked_open--;
#ifdef SERIAL_DEBUG_OPEN
	printk("block_til_ready after blocking: ttys%d, count = %d\n",
	       info->line, state->count);
#endif
#endif /* DO_THIS_LATER */
	if (retval)
		return retval;
	info->flags |= ASYNC_NORMAL_ACTIVE;
	return 0;
}

static int get_async_struct(int line, ser_info_t **ret_info)
{
	struct serial_state *sstate;

	sstate = rs_table + line;
	if (sstate->info) {
		sstate->count++;
		*ret_info = (ser_info_t *)sstate->info;
		return 0;
	}
	else {
		return -ENOMEM;
	}
}

/*
 * This routine is called whenever a serial port is opened.  It
 * enables interrupts for a serial port, linking in its async structure into
 * the IRQ chain.   It also performs the serial-specific
 * initialization for the tty structure.
 */
static int rs_360_open(struct tty_struct *tty, struct file * filp)
{
	ser_info_t	*info;
	int 		retval, line;

	line = tty->index;
	if ((line < 0) || (line >= NR_PORTS))
		return -ENODEV;
	retval = get_async_struct(line, &info);
	if (retval)
		return retval;
	if (serial_paranoia_check(info, tty->name, "rs_open"))
		return -ENODEV;

#ifdef SERIAL_DEBUG_OPEN
	printk("rs_open %s, count = %d\n", tty->name, info->state->count);
#endif
	tty->driver_data = info;
	info->tty = tty;

	/*
	 * Start up serial port
	 */
	retval = startup(info);
	if (retval)
		return retval;

	retval = block_til_ready(tty, filp, info);
	if (retval) {
#ifdef SERIAL_DEBUG_OPEN
		printk("rs_open returning after block_til_ready with %d\n",
		       retval);
#endif
		return retval;
	}

#ifdef SERIAL_DEBUG_OPEN
	printk("rs_open %s successful...", tty->name);
#endif
	return 0;
}

/*
 * /proc fs routines....
 */

static inline int line_info(char *buf, struct serial_state *state)
{
#ifdef notdef
	struct async_struct *info = state->info, scr_info;
	char	stat_buf[30], control, status;
#endif
	int	ret;

	ret = sprintf(buf, "%d: uart:%s port:%X irq:%d",
		      state->line,
		      (state->smc_scc_num & NUM_IS_SCC) ? "SCC" : "SMC",
		      (unsigned int)(state->port), state->irq);

	if (!state->port || (state->type == PORT_UNKNOWN)) {
		ret += sprintf(buf+ret, "\n");
		return ret;
	}

#ifdef notdef
	/*
	 * Figure out the current RS-232 lines
	 */
	if (!info) {
		info = &scr_info;	/* This is just for serial_{in,out} */

		info->magic = SERIAL_MAGIC;
		info->port = state->port;
		info->flags = state->flags;
		info->quot = 0;
		info->tty = 0;
	}
	local_irq_disable();
	status = serial_in(info, UART_MSR);
	control = info ? info->MCR : serial_in(info, UART_MCR);
	local_irq_enable();
	
	stat_buf[0] = 0;
	stat_buf[1] = 0;
	if (control & UART_MCR_RTS)
		strcat(stat_buf, "|RTS");
	if (status & UART_MSR_CTS)
		strcat(stat_buf, "|CTS");
	if (control & UART_MCR_DTR)
		strcat(stat_buf, "|DTR");
	if (status & UART_MSR_DSR)
		strcat(stat_buf, "|DSR");
	if (status & UART_MSR_DCD)
		strcat(stat_buf, "|CD");
	if (status & UART_MSR_RI)
		strcat(stat_buf, "|RI");

	if (info->quot) {
		ret += sprintf(buf+ret, " baud:%d",
			       state->baud_base / info->quot);
	}

	ret += sprintf(buf+ret, " tx:%d rx:%d",
		      state->icount.tx, state->icount.rx);

	if (state->icount.frame)
		ret += sprintf(buf+ret, " fe:%d", state->icount.frame);
	
	if (state->icount.parity)
		ret += sprintf(buf+ret, " pe:%d", state->icount.parity);
	
	if (state->icount.brk)
		ret += sprintf(buf+ret, " brk:%d", state->icount.brk);	

	if (state->icount.overrun)
		ret += sprintf(buf+ret, " oe:%d", state->icount.overrun);

	/*
	 * Last thing is the RS-232 status lines
	 */
	ret += sprintf(buf+ret, " %s\n", stat_buf+1);
#endif
	return ret;
}

int rs_360_read_proc(char *page, char **start, off_t off, int count,
		 int *eof, void *data)
{
	int i, len = 0;
	off_t	begin = 0;

	len += sprintf(page, "serinfo:1.0 driver:%s\n", serial_version);
	for (i = 0; i < NR_PORTS && len < 4000; i++) {
		len += line_info(page + len, &rs_table[i]);
		if (len+begin > off+count)
			goto done;
		if (len+begin < off) {
			begin += len;
			len = 0;
		}
	}
	*eof = 1;
done:
	if (off >= len+begin)
		return 0;
	*start = page + (begin-off);
	return ((count < begin+len-off) ? count : begin+len-off);
}

/*
 * ---------------------------------------------------------------------
 * rs_init() and friends
 *
 * rs_init() is called at boot-time to initialize the serial driver.
 * ---------------------------------------------------------------------
 */

/*
 * This routine prints out the appropriate serial driver version
 * number, and identifies which options were configured into this
 * driver.
 */
static _INLINE_ void show_serial_version(void)
{
 	printk(KERN_INFO "%s version %s\n", serial_name, serial_version);
}


/*
 * The serial console driver used during boot.  Note that these names
 * clash with those found in "serial.c", so we currently can't support
 * the 16xxx uarts and these at the same time.  I will fix this to become
 * an indirect function call from tty_io.c (or something).
 */

#ifdef CONFIG_SERIAL_CONSOLE

/*
 * Print a string to the serial port trying not to disturb any possible
 * real use of the port...
 */
static void my_console_write(int idx, const char *s,
				unsigned count)
{
	struct		serial_state	*ser;
	ser_info_t		*info;
	unsigned		i;
	QUICC_BD		*bdp, *bdbase;
	volatile struct smc_uart_pram	*up;
	volatile	u_char		*cp;

	ser = rs_table + idx;


	/* If the port has been initialized for general use, we have
	 * to use the buffer descriptors allocated there.  Otherwise,
	 * we simply use the single buffer allocated.
	 */
	if ((info = (ser_info_t *)ser->info) != NULL) {
		bdp = info->tx_cur;
		bdbase = info->tx_bd_base;
	}
	else {
		/* Pointer to UART in parameter ram.
		*/
		/* up = (smc_uart_t *)&cpmp->cp_dparam[ser->port]; */
		up = &pquicc->pram[ser->port].scc.pothers.idma_smc.psmc.u;

		/* Get the address of the host memory buffer.
		 */
		bdp = bdbase = (QUICC_BD *)((uint)pquicc + (uint)up->tbase);
	}

	/*
	 * We need to gracefully shut down the transmitter, disable
	 * interrupts, then send our bytes out.
	 */

	/*
	 * Now, do each character.  This is not as bad as it looks
	 * since this is a holding FIFO and not a transmitting FIFO.
	 * We could add the complexity of filling the entire transmit
	 * buffer, but we would just wait longer between accesses......
	 */
	for (i = 0; i < count; i++, s++) {
		/* Wait for transmitter fifo to empty.
		 * Ready indicates output is ready, and xmt is doing
		 * that, not that it is ready for us to send.
		 */
		while (bdp->status & BD_SC_READY);

		/* Send the character out.
		 */
		cp = bdp->buf;
		*cp = *s;
		
		bdp->length = 1;
		bdp->status |= BD_SC_READY;

		if (bdp->status & BD_SC_WRAP)
			bdp = bdbase;
		else
			bdp++;

		/* if a LF, also do CR... */
		if (*s == 10) {
			while (bdp->status & BD_SC_READY);
			/* cp = __va(bdp->buf); */
			cp = bdp->buf;
			*cp = 13;
			bdp->length = 1;
			bdp->status |= BD_SC_READY;

			if (bdp->status & BD_SC_WRAP) {
				bdp = bdbase;
			}
			else {
				bdp++;
			}
		}
	}

	/*
	 * Finally, Wait for transmitter & holding register to empty
	 *  and restore the IER
	 */
	while (bdp->status & BD_SC_READY);

	if (info)
		info->tx_cur = (QUICC_BD *)bdp;
}

static void serial_console_write(struct console *c, const char *s,
				unsigned count)
{
#ifdef CONFIG_KGDB
	/* Try to let stub handle output. Returns true if it did. */ 
	if (kgdb_output_string(s, count))
		return;
#endif
	my_console_write(c->index, s, count);
}



/*void console_print_68360(const char *p)
{
	const char *cp = p;
	int i;

	for (i=0;cp[i]!=0;i++);

	serial_console_write (p, i);

	//Comment this if you want to have a strict interrupt-driven output
	//rs_fair_output();

	return;
}*/






#ifdef CONFIG_XMON
int
xmon_360_write(const char *s, unsigned count)
{
	my_console_write(0, s, count);
	return(count);
}
#endif

#ifdef CONFIG_KGDB
void
putDebugChar(char ch)
{
	my_console_write(0, &ch, 1);
}
#endif

/*
 * Receive character from the serial port.  This only works well
 * before the port is initialized for real use.
 */
static int my_console_wait_key(int idx, int xmon, char *obuf)
{
	struct serial_state		*ser;
	u_char			c, *cp;
	ser_info_t		*info;
	QUICC_BD		*bdp;
	volatile struct smc_uart_pram	*up;
	int				i;

	ser = rs_table + idx;

	/* Get the address of the host memory buffer.
	 * If the port has been initialized for general use, we must
	 * use information from the port structure.
	 */
	if ((info = (ser_info_t *)ser->info))
		bdp = info->rx_cur;
	else
		/* bdp = (QUICC_BD *)&cpmp->cp_dpmem[up->smc_rbase]; */
		bdp = (QUICC_BD *)((uint)pquicc + (uint)up->tbase);

	/* Pointer to UART in parameter ram.
	 */
	/* up = (smc_uart_t *)&cpmp->cp_dparam[ser->port]; */
	up = &pquicc->pram[info->state->port].scc.pothers.idma_smc.psmc.u;

	/*
	 * We need to gracefully shut down the receiver, disable
	 * interrupts, then read the input.
	 * XMON just wants a poll.  If no character, return -1, else
	 * return the character.
	 */
	if (!xmon) {
		while (bdp->status & BD_SC_EMPTY);
	}
	else {
		if (bdp->status & BD_SC_EMPTY)
			return -1;
	}

	cp = (char *)bdp->buf;

	if (obuf) {
		i = c = bdp->length;
		while (i-- > 0)
			*obuf++ = *cp++;
	}
	else {
		c = *cp;
	}
	bdp->status |= BD_SC_EMPTY;

	if (info) {
		if (bdp->status & BD_SC_WRAP) {
			bdp = info->rx_bd_base;
		}
		else {
			bdp++;
		}
		info->rx_cur = (QUICC_BD *)bdp;
	}

	return((int)c);
}

static int serial_console_wait_key(struct console *co)
{
	return(my_console_wait_key(co->index, 0, NULL));
}

#ifdef CONFIG_XMON
int
xmon_360_read_poll(void)
{
	return(my_console_wait_key(0, 1, NULL));
}

int
xmon_360_read_char(void)
{
	return(my_console_wait_key(0, 0, NULL));
}
#endif

#ifdef CONFIG_KGDB
static char kgdb_buf[RX_BUF_SIZE], *kgdp;
static int kgdb_chars;

unsigned char
getDebugChar(void)
{
	if (kgdb_chars <= 0) {
		kgdb_chars = my_console_wait_key(0, 0, kgdb_buf);
		kgdp = kgdb_buf;
	}
	kgdb_chars--;

	return(*kgdp++);
}

void kgdb_interruptible(int state)
{
}
void kgdb_map_scc(void)
{
	struct		serial_state *ser;
	uint		mem_addr;
	volatile	QUICC_BD		*bdp;
	volatile	smc_uart_t	*up;

	cpmp = (cpm360_t *)&(((immap_t *)IMAP_ADDR)->im_cpm);

	/* To avoid data cache CPM DMA coherency problems, allocate a
	 * buffer in the CPM DPRAM.  This will work until the CPM and
	 * serial ports are initialized.  At that time a memory buffer
	 * will be allocated.
	 * The port is already initialized from the boot procedure, all
	 * we do here is give it a different buffer and make it a FIFO.
	 */

	ser = rs_table;

	/* Right now, assume we are using SMCs.
	*/
	up = (smc_uart_t *)&cpmp->cp_dparam[ser->port];

	/* Allocate space for an input FIFO, plus a few bytes for output.
	 * Allocate bytes to maintain word alignment.
	 */
	mem_addr = (uint)(&cpmp->cp_dpmem[0x1000]);

	/* Set the physical address of the host memory buffers in
	 * the buffer descriptors.
	 */
	bdp = (QUICC_BD *)&cpmp->cp_dpmem[up->smc_rbase];
	bdp->buf = mem_addr;

	bdp = (QUICC_BD *)&cpmp->cp_dpmem[up->smc_tbase];
	bdp->buf = mem_addr+RX_BUF_SIZE;

	up->smc_mrblr = RX_BUF_SIZE;		/* receive buffer length */
	up->smc_maxidl = RX_BUF_SIZE;
}
#endif

static struct tty_struct *serial_console_device(struct console *c, int *index)
{
	*index = c->index;
	return serial_driver;
}


struct console sercons = {
 	.name		= "ttyS",
 	.write		= serial_console_write,
 	.device		= serial_console_device,
 	.wait_key	= serial_console_wait_key,
 	.setup		= serial_console_setup,
 	.flags		= CON_PRINTBUFFER,
 	.index		= CONFIG_SERIAL_CONSOLE_PORT, 
};



/*
 *	Register console.
 */
long console_360_init(long kmem_start, long kmem_end)
{
	register_console(&sercons);
	/*register_console (console_print_68360); - 2.0.38 only required a write
      function pointer. */
	return kmem_start;
}

#endif

/* Index in baud rate table of the default console baud rate.
*/
static	int	baud_idx;

static const struct tty_operations rs_360_ops = {
	.owner = THIS_MODULE,
	.open = rs_360_open,
	.close = rs_360_close,
	.write = rs_360_write,
	.put_char = rs_360_put_char,
	.write_room = rs_360_write_room,
	.chars_in_buffer = rs_360_chars_in_buffer,
	.flush_buffer = rs_360_flush_buffer,
	.ioctl = rs_360_ioctl,
	.throttle = rs_360_throttle,
	.unthrottle = rs_360_unthrottle,
	/* .send_xchar = rs_360_send_xchar, */
	.set_termios = rs_360_set_termios,
	.stop = rs_360_stop,
	.start = rs_360_start,
	.hangup = rs_360_hangup,
	/* .wait_until_sent = rs_360_wait_until_sent, */
	/* .read_proc = rs_360_read_proc, */
	.tiocmget = rs_360_tiocmget,
	.tiocmset = rs_360_tiocmset,
};

static int __init rs_360_init(void)
{
	struct serial_state * state;
	ser_info_t	*info;
	void       *mem_addr;
	uint 		dp_addr, iobits;
	int		    i, j, idx;
	ushort		chan;
	QUICC_BD	*bdp;
	volatile	QUICC		*cp;
	volatile	struct smc_regs	*sp;
	volatile	struct smc_uart_pram	*up;
	volatile	struct scc_regs	*scp;
	volatile	struct uart_pram	*sup;
	/* volatile	immap_t		*immap; */
	
	serial_driver = alloc_tty_driver(NR_PORTS);
	if (!serial_driver)
		return -1;

	show_serial_version();

	serial_driver->name = "ttyS";
	serial_driver->major = TTY_MAJOR;
	serial_driver->minor_start = 64;
	serial_driver->type = TTY_DRIVER_TYPE_SERIAL;
	serial_driver->subtype = SERIAL_TYPE_NORMAL;
	serial_driver->init_termios = tty_std_termios;
	serial_driver->init_termios.c_cflag =
		baud_idx | CS8 | CREAD | HUPCL | CLOCAL;
	serial_driver->flags = TTY_DRIVER_REAL_RAW;
	tty_set_operations(serial_driver, &rs_360_ops);
	
	if (tty_register_driver(serial_driver))
		panic("Couldn't register serial driver\n");

	cp = pquicc;	/* Get pointer to Communication Processor */
	/* immap = (immap_t *)IMAP_ADDR; */	/* and to internal registers */


	/* Configure SCC2, SCC3, and SCC4 instead of port A parallel I/O.
	 */
	/* The "standard" configuration through the 860.
	*/
/* 	immap->im_ioport.iop_papar |= 0x00fc; */
/* 	immap->im_ioport.iop_padir &= ~0x00fc; */
/* 	immap->im_ioport.iop_paodr &= ~0x00fc; */
	cp->pio_papar |= 0x00fc;
	cp->pio_padir &= ~0x00fc;
	/* cp->pio_paodr &= ~0x00fc; */


	/* Since we don't yet do modem control, connect the port C pins
	 * as general purpose I/O.  This will assert CTS and CD for the
	 * SCC ports.
	 */
	/* FIXME: see 360um p.7-365 and 860um p.34-12 
	 * I can't make sense of these bits - mleslie*/
/* 	immap->im_ioport.iop_pcdir |= 0x03c6; */
/* 	immap->im_ioport.iop_pcpar &= ~0x03c6; */

/* 	cp->pio_pcdir |= 0x03c6; */
/* 	cp->pio_pcpar &= ~0x03c6; */



	/* Connect SCC2 and SCC3 to NMSI.  Connect BRG3 to SCC2 and
	 * BRG4 to SCC3.
	 */
	cp->si_sicr &= ~0x00ffff00;
	cp->si_sicr |=  0x001b1200;

#ifdef CONFIG_PP04
	/* Frequentis PP04 forced to RS-232 until we know better.
	 * Port C 12 and 13 low enables RS-232 on SCC3 and SCC4.
	 */
	immap->im_ioport.iop_pcdir |= 0x000c;
	immap->im_ioport.iop_pcpar &= ~0x000c;
	immap->im_ioport.iop_pcdat &= ~0x000c;

	/* This enables the TX driver.
	*/
	cp->cp_pbpar &= ~0x6000;
	cp->cp_pbdat &= ~0x6000;
#endif

	for (i = 0, state = rs_table; i < NR_PORTS; i++,state++) {
		state->magic = SSTATE_MAGIC;
		state->line = i;
		state->type = PORT_UNKNOWN;
		state->custom_divisor = 0;
		state->close_delay = 5*HZ/10;
		state->closing_wait = 30*HZ;
		state->icount.cts = state->icount.dsr = 
			state->icount.rng = state->icount.dcd = 0;
		state->icount.rx = state->icount.tx = 0;
		state->icount.frame = state->icount.parity = 0;
		state->icount.overrun = state->icount.brk = 0;
		printk(KERN_INFO "ttyS%d at irq 0x%02x is an %s\n",
		       i, (unsigned int)(state->irq),
		       (state->smc_scc_num & NUM_IS_SCC) ? "SCC" : "SMC");

#ifdef CONFIG_SERIAL_CONSOLE
		/* If we just printed the message on the console port, and
		 * we are about to initialize it for general use, we have
		 * to wait a couple of character times for the CR/NL to
		 * make it out of the transmit buffer.
		 */
		if (i == CONFIG_SERIAL_CONSOLE_PORT)
			mdelay(8);


/* 		idx = PORT_NUM(info->state->smc_scc_num); */
/* 		if (info->state->smc_scc_num & NUM_IS_SCC) */
/* 			chan = scc_chan_map[idx]; */
/* 		else */
/* 			chan = smc_chan_map[idx]; */

/* 		cp->cp_cr = mk_cr_cmd(chan, CPM_CR_STOP_TX) | CPM_CR_FLG; */
/* 		while (cp->cp_cr & CPM_CR_FLG); */

#endif
		/* info = kmalloc(sizeof(ser_info_t), GFP_KERNEL); */
		info = &quicc_ser_info[i];
		if (info) {
			memset (info, 0, sizeof(ser_info_t));
			info->magic = SERIAL_MAGIC;
			info->line = i;
			info->flags = state->flags;
			INIT_WORK(&info->tqueue, do_softint, info);
			INIT_WORK(&info->tqueue_hangup, do_serial_hangup, info);
			init_waitqueue_head(&info->open_wait);
			init_waitqueue_head(&info->close_wait);
			info->state = state;
			state->info = (struct async_struct *)info;

			/* We need to allocate a transmit and receive buffer
			 * descriptors from dual port ram, and a character
			 * buffer area from host mem.
			 */
			dp_addr = m360_cpm_dpalloc(sizeof(QUICC_BD) * RX_NUM_FIFO);

			/* Allocate space for FIFOs in the host memory.
			 *  (for now this is from a static array of buffers :(
			 */
			/* mem_addr = m360_cpm_hostalloc(RX_NUM_FIFO * RX_BUF_SIZE); */
			/* mem_addr = kmalloc (RX_NUM_FIFO * RX_BUF_SIZE, GFP_BUFFER); */
			mem_addr = &rx_buf_pool[i * RX_NUM_FIFO * RX_BUF_SIZE];

			/* Set the physical address of the host memory
			 * buffers in the buffer descriptors, and the
			 * virtual address for us to work with.
			 */
			bdp = (QUICC_BD *)((uint)pquicc + dp_addr);
			info->rx_cur = info->rx_bd_base = bdp;

			/* initialize rx buffer descriptors */
			for (j=0; j<(RX_NUM_FIFO-1); j++) {
				bdp->buf = &rx_buf_pool[(i * RX_NUM_FIFO + j ) * RX_BUF_SIZE];
				bdp->status = BD_SC_EMPTY | BD_SC_INTRPT;
				mem_addr += RX_BUF_SIZE;
				bdp++;
			}
			bdp->buf = &rx_buf_pool[(i * RX_NUM_FIFO + j ) * RX_BUF_SIZE];
			bdp->status = BD_SC_WRAP | BD_SC_EMPTY | BD_SC_INTRPT;


			idx = PORT_NUM(info->state->smc_scc_num);
			if (info->state->smc_scc_num & NUM_IS_SCC) {

#if defined (CONFIG_UCQUICC) && 1
				/* set the transceiver mode to RS232 */
				sipex_mode_bits &= ~(uint)SIPEX_MODE(idx,0x0f); /* clear current mode */
				sipex_mode_bits |= (uint)SIPEX_MODE(idx,0x02);
				*(uint *)_periph_base = sipex_mode_bits;
				/* printk ("sipex bits = 0x%08x\n", sipex_mode_bits); */
#endif
			}

			dp_addr = m360_cpm_dpalloc(sizeof(QUICC_BD) * TX_NUM_FIFO);

			/* Allocate space for FIFOs in the host memory.
			*/
			/* mem_addr = m360_cpm_hostalloc(TX_NUM_FIFO * TX_BUF_SIZE); */
			/* mem_addr = kmalloc (TX_NUM_FIFO * TX_BUF_SIZE, GFP_BUFFER); */
			mem_addr = &tx_buf_pool[i * TX_NUM_FIFO * TX_BUF_SIZE];

			/* Set the physical address of the host memory
			 * buffers in the buffer descriptors, and the
			 * virtual address for us to work with.
			 */
			/* bdp = (QUICC_BD *)&cp->cp_dpmem[dp_addr]; */
			bdp = (QUICC_BD *)((uint)pquicc + dp_addr);
			info->tx_cur = info->tx_bd_base = (QUICC_BD *)bdp;

			/* initialize tx buffer descriptors */
			for (j=0; j<(TX_NUM_FIFO-1); j++) {
				bdp->buf = &tx_buf_pool[(i * TX_NUM_FIFO + j ) * TX_BUF_SIZE];
				bdp->status = BD_SC_INTRPT;
				mem_addr += TX_BUF_SIZE;
				bdp++;
			}
			bdp->buf = &tx_buf_pool[(i * TX_NUM_FIFO + j ) * TX_BUF_SIZE];
			bdp->status = (BD_SC_WRAP | BD_SC_INTRPT);

			if (info->state->smc_scc_num & NUM_IS_SCC) {
				scp = &pquicc->scc_regs[idx];
				sup = &pquicc->pram[info->state->port].scc.pscc.u;
				sup->rbase = dp_addr;
				sup->tbase = dp_addr;

				/* Set up the uart parameters in the
				 * parameter ram.
				 */
				sup->rfcr = SMC_EB;
				sup->tfcr = SMC_EB;

				/* Set this to 1 for now, so we get single
				 * character interrupts.  Using idle charater
				 * time requires some additional tuning.
				 */
				sup->mrblr = 1;
				sup->max_idl = 0;
				sup->brkcr = 1;
				sup->parec = 0;
				sup->frmer = 0;
				sup->nosec = 0;
				sup->brkec = 0;
				sup->uaddr1 = 0;
				sup->uaddr2 = 0;
				sup->toseq = 0;
				{
					int i;
					for (i=0;i<8;i++)
						sup->cc[i] = 0x8000;
				}
				sup->rccm = 0xc0ff;

				/* Send the CPM an initialize command.
				*/
				chan = scc_chan_map[idx];

				/* execute the INIT RX & TX PARAMS command for this channel. */
				cp->cp_cr = mk_cr_cmd(chan, CPM_CR_INIT_TRX) | CPM_CR_FLG;
				while (cp->cp_cr & CPM_CR_FLG);

				/* Set UART mode, 8 bit, no parity, one stop.
				 * Enable receive and transmit.
				 */
				scp->scc_gsmr.w.high = 0;
				scp->scc_gsmr.w.low = 
					(SCC_GSMRL_MODE_UART | SCC_GSMRL_TDCR_16 | SCC_GSMRL_RDCR_16);

				/* Disable all interrupts and clear all pending
				 * events.
				 */
				scp->scc_sccm = 0;
				scp->scc_scce = 0xffff;
				scp->scc_dsr = 0x7e7e;
				scp->scc_psmr = 0x3000;

				/* If the port is the console, enable Rx and Tx.
				*/
#ifdef CONFIG_SERIAL_CONSOLE
				if (i == CONFIG_SERIAL_CONSOLE_PORT)
					scp->scc_gsmr.w.low |= (SCC_GSMRL_ENR | SCC_GSMRL_ENT);
#endif
			}
			else {
				/* Configure SMCs Tx/Rx instead of port B
				 * parallel I/O.
				 */
				up = &pquicc->pram[info->state->port].scc.pothers.idma_smc.psmc.u;
				up->rbase = dp_addr;

				iobits = 0xc0 << (idx * 4);
				cp->pip_pbpar |= iobits;
				cp->pip_pbdir &= ~iobits;
				cp->pip_pbodr &= ~iobits;


				/* Connect the baud rate generator to the
				 * SMC based upon index in rs_table.  Also
				 * make sure it is connected to NMSI.
				 */
				cp->si_simode &= ~(0xffff << (idx * 16));
				cp->si_simode |= (i << ((idx * 16) + 12));

				up->tbase = dp_addr;

				/* Set up the uart parameters in the
				 * parameter ram.
				 */
				up->rfcr = SMC_EB;
				up->tfcr = SMC_EB;

				/* Set this to 1 for now, so we get single
				 * character interrupts.  Using idle charater
				 * time requires some additional tuning.
				 */
				up->mrblr = 1;
				up->max_idl = 0;
				up->brkcr = 1;

				/* Send the CPM an initialize command.
				*/
				chan = smc_chan_map[idx];

				cp->cp_cr = mk_cr_cmd(chan,
									  CPM_CR_INIT_TRX) | CPM_CR_FLG;
#ifdef CONFIG_SERIAL_CONSOLE
				if (i == CONFIG_SERIAL_CONSOLE_PORT)
					printk("");
#endif
				while (cp->cp_cr & CPM_CR_FLG);

				/* Set UART mode, 8 bit, no parity, one stop.
				 * Enable receive and transmit.
				 */
				sp = &cp->smc_regs[idx];
				sp->smc_smcmr = smcr_mk_clen(9) | SMCMR_SM_UART;

				/* Disable all interrupts and clear all pending
				 * events.
				 */
				sp->smc_smcm = 0;
				sp->smc_smce = 0xff;

				/* If the port is the console, enable Rx and Tx.
				*/
#ifdef CONFIG_SERIAL_CONSOLE
				if (i == CONFIG_SERIAL_CONSOLE_PORT)
					sp->smc_smcmr |= SMCMR_REN | SMCMR_TEN;
#endif
			}

			/* Install interrupt handler.
			*/
			/* cpm_install_handler(IRQ_MACHSPEC | state->irq, rs_360_interrupt, info);  */
			/*request_irq(IRQ_MACHSPEC | state->irq, rs_360_interrupt, */
			request_irq(state->irq, rs_360_interrupt,
						IRQ_FLG_LOCK, "ttyS", (void *)info);

			/* Set up the baud rate generator.
			*/
			m360_cpm_setbrg(i, baud_table[baud_idx]);

		}
	}

	return 0;
}
module_init(rs_360_init);

/* This must always be called before the rs_360_init() function, otherwise
 * it blows away the port control information.
 */
//static int __init serial_console_setup( struct console *co, char *options)
int serial_console_setup( struct console *co, char *options)
{
	struct		serial_state	*ser;
	uint		mem_addr, dp_addr, bidx, idx, iobits;
	ushort		chan;
	QUICC_BD	*bdp;
	volatile	QUICC			*cp;
	volatile	struct smc_regs	*sp;
	volatile	struct scc_regs	*scp;
	volatile	struct smc_uart_pram	*up;
	volatile	struct uart_pram		*sup;

/* mleslie TODO:
 * add something to the 68k bootloader to store a desired initial console baud rate */

/* 	bd_t						*bd; */ /* a board info struct used by EPPC-bug */
/* 	bd = (bd_t *)__res; */

 	for (bidx = 0; bidx < (sizeof(baud_table) / sizeof(int)); bidx++)
	 /* if (bd->bi_baudrate == baud_table[bidx]) */
 		if (CONSOLE_BAUDRATE == baud_table[bidx])
			break;

	/* co->cflag = CREAD|CLOCAL|bidx|CS8; */
	baud_idx = bidx;

	ser = rs_table + CONFIG_SERIAL_CONSOLE_PORT;

	cp = pquicc;	/* Get pointer to Communication Processor */

	idx = PORT_NUM(ser->smc_scc_num);
	if (ser->smc_scc_num & NUM_IS_SCC) {

		/* TODO: need to set up SCC pin assignment etc. here */
		
	}
	else {
		iobits = 0xc0 << (idx * 4);
		cp->pip_pbpar |= iobits;
		cp->pip_pbdir &= ~iobits;
		cp->pip_pbodr &= ~iobits;

		/* Connect the baud rate generator to the
		 * SMC based upon index in rs_table.  Also
		 * make sure it is connected to NMSI.
		 */
		cp->si_simode &= ~(0xffff << (idx * 16));
		cp->si_simode |= (idx << ((idx * 16) + 12));
	}

	/* When we get here, the CPM has been reset, so we need
	 * to configure the port.
	 * We need to allocate a transmit and receive buffer descriptor
	 * from dual port ram, and a character buffer area from host mem.
	 */

	/* Allocate space for two buffer descriptors in the DP ram.
	*/
	dp_addr = m360_cpm_dpalloc(sizeof(QUICC_BD) * CONSOLE_NUM_FIFO);

	/* Allocate space for two 2 byte FIFOs in the host memory.
	 */
	/* mem_addr = m360_cpm_hostalloc(8); */
	mem_addr = (uint)console_fifos;


	/* Set the physical address of the host memory buffers in
	 * the buffer descriptors.
	 */
	/* bdp = (QUICC_BD *)&cp->cp_dpmem[dp_addr]; */
	bdp = (QUICC_BD *)((uint)pquicc + dp_addr);
	bdp->buf = (char *)mem_addr;
	(bdp+1)->buf = (char *)(mem_addr+4);

	/* For the receive, set empty and wrap.
	 * For transmit, set wrap.
	 */
	bdp->status = BD_SC_EMPTY | BD_SC_WRAP;
	(bdp+1)->status = BD_SC_WRAP;

	/* Set up the uart parameters in the parameter ram.
	 */
	if (ser->smc_scc_num & NUM_IS_SCC) {
		scp = &cp->scc_regs[idx];
		/* sup = (scc_uart_t *)&cp->cp_dparam[ser->port]; */
		sup = &pquicc->pram[ser->port].scc.pscc.u;

		sup->rbase = dp_addr;
		sup->tbase = dp_addr + sizeof(QUICC_BD);

		/* Set up the uart parameters in the
		 * parameter ram.
		 */
		sup->rfcr = SMC_EB;
		sup->tfcr = SMC_EB;

		/* Set this to 1 for now, so we get single
		 * character interrupts.  Using idle charater
		 * time requires some additional tuning.
		 */
		sup->mrblr = 1;
		sup->max_idl = 0;
		sup->brkcr = 1;
		sup->parec = 0;
		sup->frmer = 0;
		sup->nosec = 0;
		sup->brkec = 0;
		sup->uaddr1 = 0;
		sup->uaddr2 = 0;
		sup->toseq = 0;
		{
			int i;
			for (i=0;i<8;i++)
				sup->cc[i] = 0x8000;
		}
		sup->rccm = 0xc0ff;

		/* Send the CPM an initialize command.
		*/
		chan = scc_chan_map[idx];

		cp->cp_cr = mk_cr_cmd(chan, CPM_CR_INIT_TRX) | CPM_CR_FLG;
		while (cp->cp_cr & CPM_CR_FLG);

		/* Set UART mode, 8 bit, no parity, one stop.
		 * Enable receive and transmit.
		 */
		scp->scc_gsmr.w.high = 0;
		scp->scc_gsmr.w.low = 
			(SCC_GSMRL_MODE_UART | SCC_GSMRL_TDCR_16 | SCC_GSMRL_RDCR_16);

		/* Disable all interrupts and clear all pending
		 * events.
		 */
		scp->scc_sccm = 0;
		scp->scc_scce = 0xffff;
		scp->scc_dsr = 0x7e7e;
		scp->scc_psmr = 0x3000;

		scp->scc_gsmr.w.low |= (SCC_GSMRL_ENR | SCC_GSMRL_ENT);

	}
	else {
		/* up = (smc_uart_t *)&cp->cp_dparam[ser->port]; */
		up = &pquicc->pram[ser->port].scc.pothers.idma_smc.psmc.u;

		up->rbase = dp_addr;	/* Base of receive buffer desc. */
		up->tbase = dp_addr+sizeof(QUICC_BD);	/* Base of xmt buffer desc. */
		up->rfcr = SMC_EB;
		up->tfcr = SMC_EB;

		/* Set this to 1 for now, so we get single character interrupts.
		*/
		up->mrblr = 1;		/* receive buffer length */
		up->max_idl = 0;		/* wait forever for next char */

		/* Send the CPM an initialize command.
		*/
		chan = smc_chan_map[idx];
		cp->cp_cr = mk_cr_cmd(chan, CPM_CR_INIT_TRX) | CPM_CR_FLG;
		while (cp->cp_cr & CPM_CR_FLG);

		/* Set UART mode, 8 bit, no parity, one stop.
		 * Enable receive and transmit.
		 */
		sp = &cp->smc_regs[idx];
		sp->smc_smcmr = smcr_mk_clen(9) |  SMCMR_SM_UART;

		/* And finally, enable Rx and Tx.
		*/
		sp->smc_smcmr |= SMCMR_REN | SMCMR_TEN;
	}

	/* Set up the baud rate generator.
	*/
	/* m360_cpm_setbrg((ser - rs_table), bd->bi_baudrate); */
	m360_cpm_setbrg((ser - rs_table), CONSOLE_BAUDRATE);

	return 0;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 *  tab-width: 4
 * End:
 */
