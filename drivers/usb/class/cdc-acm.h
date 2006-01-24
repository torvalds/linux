/*
 *
 * Includes for cdc-acm.c
 *
 * Mainly take from usbnet's cdc-ether part
 *
 */

/*
 * CMSPAR, some architectures can't have space and mark parity.
 */

#ifndef CMSPAR
#define CMSPAR			0
#endif

/*
 * Major and minor numbers.
 */

#define ACM_TTY_MAJOR		166
#define ACM_TTY_MINORS		32

/*
 * Requests.
 */

#define USB_RT_ACM		(USB_TYPE_CLASS | USB_RECIP_INTERFACE)

/*
 * Output control lines.
 */

#define ACM_CTRL_DTR		0x01
#define ACM_CTRL_RTS		0x02

/*
 * Input control lines and line errors.
 */

#define ACM_CTRL_DCD		0x01
#define ACM_CTRL_DSR		0x02
#define ACM_CTRL_BRK		0x04
#define ACM_CTRL_RI		0x08

#define ACM_CTRL_FRAMING	0x10
#define ACM_CTRL_PARITY		0x20
#define ACM_CTRL_OVERRUN	0x40

/*
 * Internal driver structures.
 */

/*
 * The only reason to have several buffers is to accomodate assumptions
 * in line disciplines. They ask for empty space amount, receive our URB size,
 * and proceed to issue several 1-character writes, assuming they will fit.
 * The very first write takes a complete URB. Fortunately, this only happens
 * when processing onlcr, so we only need 2 buffers.
 */
#define ACM_NWB  2
#define ACM_NRU  16
#define ACM_NRB  16

struct acm_wb {
	unsigned char *buf;
	dma_addr_t dmah;
	int len;
	int use;
};

struct acm_rb {
	struct list_head	list;
	int			size;
	unsigned char		*base;
	dma_addr_t		dma;
};

struct acm_ru {
	struct list_head	list;
	struct acm_rb		*buffer;
	struct urb		*urb;
	struct acm		*instance;
};

struct acm {
	struct usb_device *dev;				/* the corresponding usb device */
	struct usb_interface *control;			/* control interface */
	struct usb_interface *data;			/* data interface */
	struct tty_struct *tty;				/* the corresponding tty */
	struct urb *ctrlurb, *writeurb;			/* urbs */
	u8 *ctrl_buffer;				/* buffers of urbs */
	dma_addr_t ctrl_dma;				/* dma handles of buffers */
	struct acm_wb wb[ACM_NWB];
	struct acm_ru ru[ACM_NRU];
	struct acm_rb rb[ACM_NRB];
	int rx_endpoint;
	spinlock_t read_lock;
	struct list_head spare_read_urbs;
	struct list_head spare_read_bufs;
	struct list_head filled_read_bufs;
	int write_current;				/* current write buffer */
	int write_used;					/* number of non-empty write buffers */
	int write_ready;				/* write urb is not running */
	spinlock_t write_lock;
	struct usb_cdc_line_coding line;		/* bits, stop, parity */
	struct work_struct work;			/* work queue entry for line discipline waking up */
	struct tasklet_struct urb_task;                 /* rx processing */
	spinlock_t throttle_lock;			/* synchronize throtteling and read callback */
	unsigned int ctrlin;				/* input control lines (DCD, DSR, RI, break, overruns) */
	unsigned int ctrlout;				/* output control lines (DTR, RTS) */
	unsigned int writesize;				/* max packet size for the output bulk endpoint */
	unsigned int readsize,ctrlsize;			/* buffer sizes for freeing */
	unsigned int used;				/* someone has this acm's device open */
	unsigned int minor;				/* acm minor number */
	unsigned char throttle;				/* throttled by tty layer */
	unsigned char clocal;				/* termios CLOCAL */
	unsigned int ctrl_caps;				/* control capabilities from the class specific header */
};

#define CDC_DATA_INTERFACE_TYPE	0x0a

/* constants describing various quirks and errors */
#define NO_UNION_NORMAL			1
