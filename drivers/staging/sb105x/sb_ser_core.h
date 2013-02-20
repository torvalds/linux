#include <linux/wait.h>

#define UART_CONFIG_TYPE	(1 << 0)
#define UART_CONFIG_IRQ		(1 << 1)
#define UPIO_PORT		(0)
#define UPIO_HUB6		(1)
#define UPIO_MEM		(2)
#define UPIO_MEM32		(3)
#define UPIO_AU			(4)			/* Au1x00 type IO */
#define UPIO_TSI		(5)			/* Tsi108/109 type IO */
#define UPF_FOURPORT		(1 << 1)
#define UPF_SAK			(1 << 2)
#define UPF_SPD_MASK		(0x1030)
#define UPF_SPD_HI		(0x0010)
#define UPF_SPD_VHI		(0x0020)
#define UPF_SPD_CUST		(0x0030)
#define UPF_SPD_SHI		(0x1000)
#define UPF_SPD_WARP		(0x1010)
#define UPF_SKIP_TEST		(1 << 6)
#define UPF_AUTO_IRQ		(1 << 7)
#define UPF_HARDPPS_CD		(1 << 11)
#define UPF_LOW_LATENCY		(1 << 13)
#define UPF_BUGGY_UART		(1 << 14)
#define UPF_MAGIC_MULTIPLIER	(1 << 16)
#define UPF_CONS_FLOW		(1 << 23)
#define UPF_SHARE_IRQ		(1 << 24)
#define UPF_BOOT_AUTOCONF	(1 << 28)
#define UPF_DEAD		(1 << 30)
#define UPF_IOREMAP		(1 << 31)
#define UPF_CHANGE_MASK		(0x17fff)
#define UPF_USR_MASK		(UPF_SPD_MASK|UPF_LOW_LATENCY)
#define USF_CLOSING_WAIT_INF	(0)
#define USF_CLOSING_WAIT_NONE	(~0U)

#define UART_XMIT_SIZE	PAGE_SIZE

#define UIF_CHECK_CD		(1 << 25)
#define UIF_CTS_FLOW		(1 << 26)
#define UIF_NORMAL_ACTIVE	(1 << 29)
#define UIF_INITIALIZED		(1 << 31)
#define UIF_SUSPENDED		(1 << 30)

#define WAKEUP_CHARS		256

#define uart_circ_empty(circ)		((circ)->head == (circ)->tail)
#define uart_circ_clear(circ)		((circ)->head = (circ)->tail = 0)

#define uart_circ_chars_pending(circ)	\
	(CIRC_CNT((circ)->head, (circ)->tail, UART_XMIT_SIZE))

#define uart_circ_chars_free(circ)	\
	(CIRC_SPACE((circ)->head, (circ)->tail, UART_XMIT_SIZE))

#define uart_tx_stopped(port)		\
	((port)->info->tty->stopped || (port)->info->tty->hw_stopped)

#define UART_ENABLE_MS(port,cflag)	((port)->flags & UPF_HARDPPS_CD || \
					 (cflag) & CRTSCTS || \
					 !((cflag) & CLOCAL))


struct sb_uart_port;
struct sb_uart_info;
struct serial_struct;
struct device;

struct sb_uart_ops {
	unsigned int	(*tx_empty)(struct sb_uart_port *);
	void		(*set_mctrl)(struct sb_uart_port *, unsigned int mctrl);
	unsigned int	(*get_mctrl)(struct sb_uart_port *);
	void		(*stop_tx)(struct sb_uart_port *);
	void		(*start_tx)(struct sb_uart_port *);
	void		(*send_xchar)(struct sb_uart_port *, char ch);
	void		(*stop_rx)(struct sb_uart_port *);
	void		(*enable_ms)(struct sb_uart_port *);
	void		(*break_ctl)(struct sb_uart_port *, int ctl);
	int		(*startup)(struct sb_uart_port *);
	void		(*shutdown)(struct sb_uart_port *);
	void		(*set_termios)(struct sb_uart_port *, struct MP_TERMIOS *new,
				       struct MP_TERMIOS *old);
	void		(*pm)(struct sb_uart_port *, unsigned int state,
			      unsigned int oldstate);
	int		(*set_wake)(struct sb_uart_port *, unsigned int state);

	const char *(*type)(struct sb_uart_port *);

	void		(*release_port)(struct sb_uart_port *);

	int		(*request_port)(struct sb_uart_port *);
	void		(*config_port)(struct sb_uart_port *, int);
	int		(*verify_port)(struct sb_uart_port *, struct serial_struct *);
	int		(*ioctl)(struct sb_uart_port *, unsigned int, unsigned long);
};


struct sb_uart_icount {
	__u32	cts;
	__u32	dsr;
	__u32	rng;
	__u32	dcd;
	__u32	rx;
	__u32	tx;
	__u32	frame;
	__u32	overrun;
	__u32	parity;
	__u32	brk;
	__u32	buf_overrun;
};
typedef unsigned int  upf_t;

struct sb_uart_port {
	spinlock_t		lock;			/* port lock */
	unsigned int		iobase;			/* in/out[bwl] */
	unsigned char __iomem	*membase;		/* read/write[bwl] */
	unsigned int		irq;			/* irq number */
	unsigned int		uartclk;		/* base uart clock */
	unsigned int		fifosize;		/* tx fifo size */
	unsigned char		x_char;			/* xon/xoff char */
	unsigned char		regshift;		/* reg offset shift */
	unsigned char		iotype;			/* io access style */
	unsigned char		unused1;


	unsigned int		read_status_mask;	/* driver specific */
	unsigned int		ignore_status_mask;	/* driver specific */
	struct sb_uart_info	*info;			/* pointer to parent info */
	struct sb_uart_icount	icount;			/* statistics */

	struct console		*cons;			/* struct console, if any */
#ifdef CONFIG_SERIAL_CORE_CONSOLE
	unsigned long		sysrq;			/* sysrq timeout */
#endif

	upf_t			flags;

	unsigned int		mctrl;			/* current modem ctrl settings */
	unsigned int		timeout;		/* character-based timeout */
	unsigned int		type;			/* port type */
	const struct sb_uart_ops	*ops;
	unsigned int		custom_divisor;
	unsigned int		line;			/* port index */
	unsigned long		mapbase;		/* for ioremap */
	struct device		*dev;			/* parent device */
	unsigned char		hub6;			/* this should be in the 8250 driver */
	unsigned char		unused[3];
};

#define mdmode			unused[2]
#define MDMODE_ADDR		0x1
#define MDMODE_ENABLE	0x2
#define MDMODE_AUTO		0x4
#define MDMODE_ADDRSEND	0x8

struct sb_uart_state {
	unsigned int		close_delay;		/* msec */
	unsigned int		closing_wait;		/* msec */


	int			count;
	int			pm_state;
	struct sb_uart_info	*info;
	struct sb_uart_port	*port;

	struct mutex		mutex;
};

typedef unsigned int  uif_t;

struct sb_uart_info {
	struct tty_struct	*tty;
	struct circ_buf		xmit;
	uif_t			flags;

	int			blocked_open;

	struct tasklet_struct	tlet;

	wait_queue_head_t	open_wait;
	wait_queue_head_t	delta_msr_wait;
};


struct module;
struct tty_driver;

struct uart_driver {
	struct module		*owner;
	const char		*driver_name;
	const char		*dev_name;
	int			 major;
	int			 minor;
	int			 nr;
	struct console		*cons;

	struct sb_uart_state	*state;
        struct tty_driver               *tty_driver;
};

void sb_uart_write_wakeup(struct sb_uart_port *port)
{
    struct sb_uart_info *info = port->info;
    tasklet_schedule(&info->tlet);
}

void sb_uart_update_timeout(struct sb_uart_port *port, unsigned int cflag,
			 unsigned int baud)
{
    unsigned int bits;

    switch (cflag & CSIZE)
    {
        case CS5:
            bits = 7;
            break;

        case CS6:
            bits = 8;
            break;

        case CS7:
            bits = 9;
            break;

        default:
            bits = 10;
            break;
    }

    if (cflag & CSTOPB)
    {
        bits++;
    }

    if (cflag & PARENB)
    {
        bits++;
    }

    bits = bits * port->fifosize;

    port->timeout = (HZ * bits) / baud + HZ/50;
}
unsigned int sb_uart_get_baud_rate(struct sb_uart_port *port, struct MP_TERMIOS *termios,
				struct MP_TERMIOS *old, unsigned int min,
				unsigned int max)
{
        unsigned int try, baud, altbaud = 38400;
        upf_t flags = port->flags & UPF_SPD_MASK;

        if (flags == UPF_SPD_HI)
                altbaud = 57600;
        if (flags == UPF_SPD_VHI)
                altbaud = 115200;
        if (flags == UPF_SPD_SHI)
                altbaud = 230400;
        if (flags == UPF_SPD_WARP)
                altbaud = 460800;

        for (try = 0; try < 2; try++) {

                switch (termios->c_cflag & (CBAUD | CBAUDEX))
                {
                	case B921600    : baud = 921600;    break;
                	case B460800    : baud = 460800;    break;
                	case B230400    : baud = 230400;    break;
                	case B115200    : baud = 115200;    break;
                	case B57600     : baud = 57600;     break;
                	case B38400     : baud = 38400;     break;
                	case B19200     : baud = 19200;     break;
                	case B9600      : baud = 9600;      break;
                	case B4800      : baud = 4800;      break;
                	case B2400      : baud = 2400;      break;
                	case B1800      : baud = 1800;      break;
                	case B1200      : baud = 1200;      break;
                	case B600       : baud = 600;       break;
                	case B300       : baud = 300;       break;
                        case B200       : baud = 200;       break;
                	case B150       : baud = 150;       break;
                	case B134       : baud = 134;       break;
                	case B110       : baud = 110;       break;
                	case B75        : baud = 75;        break;
                	case B50        : baud = 50;        break;
                	default         : baud = 9600;      break;
                }

                if (baud == 38400)
                        baud = altbaud;

                if (baud == 0)
                        baud = 9600;

                if (baud >= min && baud <= max)
                        return baud;

                termios->c_cflag &= ~CBAUD;
                if (old) {
                        termios->c_cflag |= old->c_cflag & CBAUD;
                        old = NULL;
                        continue;
                }

                termios->c_cflag |= B9600;
        }

        return 0;
}
unsigned int sb_uart_get_divisor(struct sb_uart_port *port, unsigned int baud)
{
        unsigned int quot;

        if (baud == 38400 && (port->flags & UPF_SPD_MASK) == UPF_SPD_CUST)
                quot = port->custom_divisor;
        else
                quot = (port->uartclk + (8 * baud)) / (16 * baud);

        return quot;
}



static inline int sb_uart_handle_break(struct sb_uart_port *port)
{
	struct sb_uart_info *info = port->info;

	if (port->flags & UPF_SAK)
		do_SAK(info->tty);
	return 0;
}

static inline void sb_uart_handle_dcd_change(struct sb_uart_port *port, unsigned int status)
{
	struct sb_uart_info *info = port->info;

	port->icount.dcd++;

	if (info->flags & UIF_CHECK_CD) {
		if (status)
			wake_up_interruptible(&info->open_wait);
		else if (info->tty)
			tty_hangup(info->tty);
	}
}

static inline void sb_uart_handle_cts_change(struct sb_uart_port *port, unsigned int status)
{
	struct sb_uart_info *info = port->info;
	struct tty_struct *tty = info->tty;

	port->icount.cts++;

	if (info->flags & UIF_CTS_FLOW) {
		if (tty->hw_stopped) {
			if (status) {
				tty->hw_stopped = 0;
				port->ops->start_tx(port);
				sb_uart_write_wakeup(port);
			}
		} else {
			if (!status) {
				tty->hw_stopped = 1;
				port->ops->stop_tx(port);
			}
		}
	}
}



