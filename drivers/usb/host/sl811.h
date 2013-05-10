/*
 * SL811HS register declarations and HCD data structures
 *
 * Copyright (C) 2004 Psion Teklogix
 * Copyright (C) 2004 David Brownell
 * Copyright (C) 2001 Cypress Semiconductor Inc. 
 */

/*
 * SL811HS has transfer registers, and control registers.  In host/master
 * mode one set of registers is used; in peripheral/slave mode, another.
 *  - SL11H only has some "A" transfer registers from 0x00-0x04
 *  - SL811HS also has "B" registers from 0x08-0x0c
 *  - SL811S (or HS in slave mode) has four A+B sets, at 00, 10, 20, 30
 */

#define SL811_EP_A(base)	((base) + 0)
#define SL811_EP_B(base)	((base) + 8)

#define SL811_HOST_BUF		0x00
#define SL811_PERIPH_EP0	0x00
#define SL811_PERIPH_EP1	0x10
#define SL811_PERIPH_EP2	0x20
#define SL811_PERIPH_EP3	0x30


/* TRANSFER REGISTERS:  host and peripheral sides are similar
 * except for the control models (master vs slave).
 */
#define SL11H_HOSTCTLREG	0
#	define SL11H_HCTLMASK_ARM	0x01
#	define SL11H_HCTLMASK_ENABLE	0x02
#	define SL11H_HCTLMASK_IN	0x00
#	define SL11H_HCTLMASK_OUT	0x04
#	define SL11H_HCTLMASK_ISOCH	0x10
#	define SL11H_HCTLMASK_AFTERSOF	0x20
#	define SL11H_HCTLMASK_TOGGLE	0x40
#	define SL11H_HCTLMASK_PREAMBLE	0x80
#define SL11H_BUFADDRREG	1
#define SL11H_BUFLNTHREG	2
#define SL11H_PKTSTATREG	3	/* read */
#	define SL11H_STATMASK_ACK	0x01
#	define SL11H_STATMASK_ERROR	0x02
#	define SL11H_STATMASK_TMOUT	0x04
#	define SL11H_STATMASK_SEQ	0x08
#	define SL11H_STATMASK_SETUP	0x10
#	define SL11H_STATMASK_OVF	0x20
#	define SL11H_STATMASK_NAK	0x40
#	define SL11H_STATMASK_STALL	0x80
#define SL11H_PIDEPREG		3	/* write */
#	define	SL_SETUP	0xd0
#	define	SL_IN		0x90
#	define	SL_OUT		0x10
#	define	SL_SOF		0x50
#	define	SL_PREAMBLE	0xc0
#	define	SL_NAK		0xa0
#	define	SL_STALL	0xe0
#	define	SL_DATA0	0x30
#	define	SL_DATA1	0xb0
#define SL11H_XFERCNTREG	4	/* read */
#define SL11H_DEVADDRREG	4	/* write */


/* CONTROL REGISTERS:  host and peripheral are very different.
 */
#define SL11H_CTLREG1		5
#	define SL11H_CTL1MASK_SOF_ENA	0x01
#	define SL11H_CTL1MASK_FORCE	0x18
#		define SL11H_CTL1MASK_NORMAL	0x00
#		define SL11H_CTL1MASK_SE0	0x08	/* reset */
#		define SL11H_CTL1MASK_J		0x10
#		define SL11H_CTL1MASK_K		0x18	/* resume */
#	define SL11H_CTL1MASK_LSPD	0x20
#	define SL11H_CTL1MASK_SUSPEND	0x40
#define SL11H_IRQ_ENABLE	6
#	define SL11H_INTMASK_DONE_A	0x01
#	define SL11H_INTMASK_DONE_B	0x02
#	define SL11H_INTMASK_SOFINTR	0x10
#	define SL11H_INTMASK_INSRMV	0x20	/* to/from SE0 */
#	define SL11H_INTMASK_RD		0x40
#	define SL11H_INTMASK_DP		0x80	/* only in INTSTATREG */
#define SL11S_ADDRESS		7

/* 0x08-0x0c are for the B buffer (not in SL11) */

#define SL11H_IRQ_STATUS	0x0D	/* write to ack */
#define SL11H_HWREVREG		0x0E	/* read */
#	define SL11H_HWRMASK_HWREV	0xF0
#define SL11H_SOFLOWREG		0x0E	/* write */
#define SL11H_SOFTMRREG		0x0F	/* read */

/* a write to this register enables SL811HS features.
 * HOST flag presumably overrides the chip input signal?
 */
#define SL811HS_CTLREG2		0x0F
#	define SL811HS_CTL2MASK_SOF_MASK	0x3F
#	define SL811HS_CTL2MASK_DSWAP		0x40
#	define SL811HS_CTL2MASK_HOST		0x80

#define SL811HS_CTL2_INIT	(SL811HS_CTL2MASK_HOST | 0x2e)


/* DATA BUFFERS: registers from 0x10..0xff are for data buffers;
 * that's 240 bytes, which we'll split evenly between A and B sides.
 * Only ISO can use more than 64 bytes per packet.
 * (The SL11S has 0x40..0xff for buffers.)
 */
#define H_MAXPACKET	120		/* bytes in A or B fifos */

#define SL11H_DATA_START	0x10
#define	SL811HS_PACKET_BUF(is_a)	((is_a) \
		? SL11H_DATA_START \
		: (SL11H_DATA_START + H_MAXPACKET))

/*-------------------------------------------------------------------------*/

#define	LOG2_PERIODIC_SIZE	5	/* arbitrary; this matches OHCI */
#define	PERIODIC_SIZE		(1 << LOG2_PERIODIC_SIZE)

struct sl811 {
	spinlock_t		lock;
	void __iomem		*addr_reg;
	void __iomem		*data_reg;
	struct sl811_platform_data	*board;
	struct proc_dir_entry	*pde;

	unsigned long		stat_insrmv;
	unsigned long		stat_wake;
	unsigned long		stat_sof;
	unsigned long		stat_a;
	unsigned long		stat_b;
	unsigned long		stat_lost;
	unsigned long		stat_overrun;

	/* sw model */
	struct timer_list	timer;
	struct sl811h_ep	*next_periodic;
	struct sl811h_ep	*next_async;

	struct sl811h_ep	*active_a;
	unsigned long		jiffies_a;
	struct sl811h_ep	*active_b;
	unsigned long		jiffies_b;

	u32			port1;
	u8			ctrl1, ctrl2, irq_enable;
	u16			frame;

	/* async schedule: control, bulk */
	struct list_head	async;

	/* periodic schedule: interrupt, iso */
	u16			load[PERIODIC_SIZE];
	struct sl811h_ep	*periodic[PERIODIC_SIZE];
	unsigned		periodic_count;
};

static inline struct sl811 *hcd_to_sl811(struct usb_hcd *hcd)
{
	return (struct sl811 *) (hcd->hcd_priv);
}

static inline struct usb_hcd *sl811_to_hcd(struct sl811 *sl811)
{
	return container_of((void *) sl811, struct usb_hcd, hcd_priv);
}

struct sl811h_ep {
	struct usb_host_endpoint *hep;
	struct usb_device	*udev;

	u8			defctrl;
	u8			maxpacket;
	u8			epnum;
	u8			nextpid;

	u16			error_count;
	u16			nak_count;
	u16			length;		/* of current packet */

	/* periodic schedule */
	u16			period;
	u16			branch;
	u16			load;
	struct sl811h_ep	*next;

	/* async schedule */
	struct list_head	schedule;
};

/*-------------------------------------------------------------------------*/

/* These register utilities should work for the SL811S register API too
 * NOTE:  caller must hold sl811->lock.
 */

static inline u8 sl811_read(struct sl811 *sl811, int reg)
{
	writeb(reg, sl811->addr_reg);
	return readb(sl811->data_reg);
}

static inline void sl811_write(struct sl811 *sl811, int reg, u8 val)
{
	writeb(reg, sl811->addr_reg);
	writeb(val, sl811->data_reg);
}

static inline void
sl811_write_buf(struct sl811 *sl811, int addr, const void *buf, size_t count)
{
	const u8	*data;
	void __iomem	*data_reg;

	if (!count)
		return;
	writeb(addr, sl811->addr_reg);

	data = buf;
	data_reg = sl811->data_reg;
	do {
		writeb(*data++, data_reg);
	} while (--count);
}

static inline void
sl811_read_buf(struct sl811 *sl811, int addr, void *buf, size_t count)
{
	u8 		*data;
	void __iomem	*data_reg;

	if (!count)
		return;
	writeb(addr, sl811->addr_reg);

	data = buf;
	data_reg = sl811->data_reg;
	do {
		*data++ = readb(data_reg);
	} while (--count);
}

/*-------------------------------------------------------------------------*/

#ifdef DEBUG
#define DBG(stuff...)		printk(KERN_DEBUG "sl811: " stuff)
#else
#define DBG(stuff...)		do{}while(0)
#endif

#ifdef VERBOSE
#    define VDBG		DBG
#else
#    define VDBG(stuff...)	do{}while(0)
#endif

#ifdef PACKET_TRACE
#    define PACKET		VDBG
#else
#    define PACKET(stuff...)	do{}while(0)
#endif

#define ERR(stuff...)		printk(KERN_ERR "sl811: " stuff)
#define WARNING(stuff...)	printk(KERN_WARNING "sl811: " stuff)
#define INFO(stuff...)		printk(KERN_INFO "sl811: " stuff)

