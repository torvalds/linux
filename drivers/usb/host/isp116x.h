/*
 * ISP116x register declarations and HCD data structures
 *
 * Copyright (C) 2005 Olav Kongas <ok@artecdesign.ee>
 * Portions:
 * Copyright (C) 2004 Lothar Wassmann
 * Copyright (C) 2004 Psion Teklogix
 * Copyright (C) 2004 David Brownell
 */

/* us of 1ms frame */
#define  MAX_LOAD_LIMIT		850

/* Full speed: max # of bytes to transfer for a single urb
   at a time must be < 1024 && must be multiple of 64.
   832 allows transferring 4kiB within 5 frames. */
#define MAX_TRANSFER_SIZE_FULLSPEED	832

/* Low speed: there is no reason to schedule in very big
   chunks; often the requested long transfers are for
   string descriptors containing short strings. */
#define MAX_TRANSFER_SIZE_LOWSPEED	64

/* Bytetime (us), a rough indication of how much time it
   would take to transfer a byte of useful data over USB */
#define BYTE_TIME_FULLSPEED	1
#define BYTE_TIME_LOWSPEED	20

/* Buffer sizes */
#define ISP116x_BUF_SIZE	4096
#define ISP116x_ITL_BUFSIZE	0
#define ISP116x_ATL_BUFSIZE	((ISP116x_BUF_SIZE) - 2*(ISP116x_ITL_BUFSIZE))

#define ISP116x_WRITE_OFFSET	0x80

/*------------ ISP116x registers/bits ------------*/
#define	HCREVISION	0x00
#define	HCCONTROL	0x01
#define		HCCONTROL_HCFS	(3 << 6)	/* host controller
						   functional state */
#define		HCCONTROL_USB_RESET	(0 << 6)
#define		HCCONTROL_USB_RESUME	(1 << 6)
#define		HCCONTROL_USB_OPER	(2 << 6)
#define		HCCONTROL_USB_SUSPEND	(3 << 6)
#define		HCCONTROL_RWC	(1 << 9)	/* remote wakeup connected */
#define		HCCONTROL_RWE	(1 << 10)	/* remote wakeup enable */
#define	HCCMDSTAT	0x02
#define		HCCMDSTAT_HCR	(1 << 0)	/* host controller reset */
#define		HCCMDSTAT_SOC	(3 << 16)	/* scheduling overrun count */
#define	HCINTSTAT	0x03
#define		HCINT_SO	(1 << 0)	/* scheduling overrun */
#define		HCINT_WDH	(1 << 1)	/* writeback of done_head */
#define		HCINT_SF	(1 << 2)	/* start frame */
#define		HCINT_RD	(1 << 3)	/* resume detect */
#define		HCINT_UE	(1 << 4)	/* unrecoverable error */
#define		HCINT_FNO	(1 << 5)	/* frame number overflow */
#define		HCINT_RHSC	(1 << 6)	/* root hub status change */
#define		HCINT_OC	(1 << 30)	/* ownership change */
#define		HCINT_MIE	(1 << 31)	/* master interrupt enable */
#define	HCINTENB	0x04
#define	HCINTDIS	0x05
#define	HCFMINTVL	0x0d
#define	HCFMREM		0x0e
#define	HCFMNUM		0x0f
#define	HCLSTHRESH	0x11
#define	HCRHDESCA	0x12
#define		RH_A_NDP	(0x3 << 0)	/* # downstream ports */
#define		RH_A_PSM	(1 << 8)	/* power switching mode */
#define		RH_A_NPS	(1 << 9)	/* no power switching */
#define		RH_A_DT		(1 << 10)	/* device type (mbz) */
#define		RH_A_OCPM	(1 << 11)	/* overcurrent protection
						   mode */
#define		RH_A_NOCP	(1 << 12)	/* no overcurrent protection */
#define		RH_A_POTPGT	(0xff << 24)	/* power on -> power good
						   time */
#define	HCRHDESCB	0x13
#define		RH_B_DR		(0xffff << 0)	/* device removable flags */
#define		RH_B_PPCM	(0xffff << 16)	/* port power control mask */
#define	HCRHSTATUS	0x14
#define		RH_HS_LPS	(1 << 0)	/* local power status */
#define		RH_HS_OCI	(1 << 1)	/* over current indicator */
#define		RH_HS_DRWE	(1 << 15)	/* device remote wakeup
						   enable */
#define		RH_HS_LPSC	(1 << 16)	/* local power status change */
#define		RH_HS_OCIC	(1 << 17)	/* over current indicator
						   change */
#define		RH_HS_CRWE	(1 << 31)	/* clear remote wakeup
						   enable */
#define	HCRHPORT1	0x15
#define		RH_PS_CCS	(1 << 0)	/* current connect status */
#define		RH_PS_PES	(1 << 1)	/* port enable status */
#define		RH_PS_PSS	(1 << 2)	/* port suspend status */
#define		RH_PS_POCI	(1 << 3)	/* port over current
						   indicator */
#define		RH_PS_PRS	(1 << 4)	/* port reset status */
#define		RH_PS_PPS	(1 << 8)	/* port power status */
#define		RH_PS_LSDA	(1 << 9)	/* low speed device attached */
#define		RH_PS_CSC	(1 << 16)	/* connect status change */
#define		RH_PS_PESC	(1 << 17)	/* port enable status change */
#define		RH_PS_PSSC	(1 << 18)	/* port suspend status
						   change */
#define		RH_PS_OCIC	(1 << 19)	/* over current indicator
						   change */
#define		RH_PS_PRSC	(1 << 20)	/* port reset status change */
#define		HCRHPORT_CLRMASK	(0x1f << 16)
#define	HCRHPORT2	0x16
#define	HCHWCFG		0x20
#define		HCHWCFG_15KRSEL		(1 << 12)
#define		HCHWCFG_CLKNOTSTOP	(1 << 11)
#define		HCHWCFG_ANALOG_OC	(1 << 10)
#define		HCHWCFG_DACK_MODE	(1 << 8)
#define		HCHWCFG_EOT_POL		(1 << 7)
#define		HCHWCFG_DACK_POL	(1 << 6)
#define		HCHWCFG_DREQ_POL	(1 << 5)
#define		HCHWCFG_DBWIDTH_MASK	(0x03 << 3)
#define		HCHWCFG_DBWIDTH(n)	(((n) << 3) & HCHWCFG_DBWIDTH_MASK)
#define		HCHWCFG_INT_POL		(1 << 2)
#define		HCHWCFG_INT_TRIGGER	(1 << 1)
#define		HCHWCFG_INT_ENABLE	(1 << 0)
#define	HCDMACFG	0x21
#define		HCDMACFG_BURST_LEN_MASK	(0x03 << 5)
#define		HCDMACFG_BURST_LEN(n)	(((n) << 5) & HCDMACFG_BURST_LEN_MASK)
#define		HCDMACFG_BURST_LEN_1	HCDMACFG_BURST_LEN(0)
#define		HCDMACFG_BURST_LEN_4	HCDMACFG_BURST_LEN(1)
#define		HCDMACFG_BURST_LEN_8	HCDMACFG_BURST_LEN(2)
#define		HCDMACFG_DMA_ENABLE	(1 << 4)
#define		HCDMACFG_BUF_TYPE_MASK	(0x07 << 1)
#define		HCDMACFG_CTR_SEL	(1 << 2)
#define		HCDMACFG_ITLATL_SEL	(1 << 1)
#define		HCDMACFG_DMA_RW_SELECT	(1 << 0)
#define	HCXFERCTR	0x22
#define	HCuPINT		0x24
#define		HCuPINT_SOF		(1 << 0)
#define		HCuPINT_ATL		(1 << 1)
#define		HCuPINT_AIIEOT		(1 << 2)
#define		HCuPINT_OPR		(1 << 4)
#define		HCuPINT_SUSP		(1 << 5)
#define		HCuPINT_CLKRDY		(1 << 6)
#define	HCuPINTENB	0x25
#define	HCCHIPID	0x27
#define		HCCHIPID_MASK		0xff00
#define		HCCHIPID_MAGIC		0x6100
#define	HCSCRATCH	0x28
#define	HCSWRES		0x29
#define		HCSWRES_MAGIC		0x00f6
#define	HCITLBUFLEN	0x2a
#define	HCATLBUFLEN	0x2b
#define	HCBUFSTAT	0x2c
#define		HCBUFSTAT_ITL0_FULL	(1 << 0)
#define		HCBUFSTAT_ITL1_FULL	(1 << 1)
#define		HCBUFSTAT_ATL_FULL	(1 << 2)
#define		HCBUFSTAT_ITL0_DONE	(1 << 3)
#define		HCBUFSTAT_ITL1_DONE	(1 << 4)
#define		HCBUFSTAT_ATL_DONE	(1 << 5)
#define	HCRDITL0LEN	0x2d
#define	HCRDITL1LEN	0x2e
#define	HCITLPORT	0x40
#define	HCATLPORT	0x41

/* Philips transfer descriptor */
struct ptd {
	u16 count;
#define	PTD_COUNT_MSK	(0x3ff << 0)
#define	PTD_TOGGLE_MSK	(1 << 10)
#define	PTD_ACTIVE_MSK	(1 << 11)
#define	PTD_CC_MSK	(0xf << 12)
	u16 mps;
#define	PTD_MPS_MSK	(0x3ff << 0)
#define	PTD_SPD_MSK	(1 << 10)
#define	PTD_LAST_MSK	(1 << 11)
#define	PTD_EP_MSK	(0xf << 12)
	u16 len;
#define	PTD_LEN_MSK	(0x3ff << 0)
#define	PTD_DIR_MSK	(3 << 10)
#define	PTD_DIR_SETUP	(0)
#define	PTD_DIR_OUT	(1)
#define	PTD_DIR_IN	(2)
#define	PTD_B5_5_MSK	(1 << 13)
	u16 faddr;
#define	PTD_FA_MSK	(0x7f << 0)
#define	PTD_FMT_MSK	(1 << 7)
} __attribute__ ((packed, aligned(2)));

/* PTD accessor macros. */
#define PTD_GET_COUNT(p)	(((p)->count & PTD_COUNT_MSK) >> 0)
#define PTD_COUNT(v)		(((v) << 0) & PTD_COUNT_MSK)
#define PTD_GET_TOGGLE(p)	(((p)->count & PTD_TOGGLE_MSK) >> 10)
#define PTD_TOGGLE(v)		(((v) << 10) & PTD_TOGGLE_MSK)
#define PTD_GET_ACTIVE(p)	(((p)->count & PTD_ACTIVE_MSK) >> 11)
#define PTD_ACTIVE(v)		(((v) << 11) & PTD_ACTIVE_MSK)
#define PTD_GET_CC(p)		(((p)->count & PTD_CC_MSK) >> 12)
#define PTD_CC(v)		(((v) << 12) & PTD_CC_MSK)
#define PTD_GET_MPS(p)		(((p)->mps & PTD_MPS_MSK) >> 0)
#define PTD_MPS(v)		(((v) << 0) & PTD_MPS_MSK)
#define PTD_GET_SPD(p)		(((p)->mps & PTD_SPD_MSK) >> 10)
#define PTD_SPD(v)		(((v) << 10) & PTD_SPD_MSK)
#define PTD_GET_LAST(p)		(((p)->mps & PTD_LAST_MSK) >> 11)
#define PTD_LAST(v)		(((v) << 11) & PTD_LAST_MSK)
#define PTD_GET_EP(p)		(((p)->mps & PTD_EP_MSK) >> 12)
#define PTD_EP(v)		(((v) << 12) & PTD_EP_MSK)
#define PTD_GET_LEN(p)		(((p)->len & PTD_LEN_MSK) >> 0)
#define PTD_LEN(v)		(((v) << 0) & PTD_LEN_MSK)
#define PTD_GET_DIR(p)		(((p)->len & PTD_DIR_MSK) >> 10)
#define PTD_DIR(v)		(((v) << 10) & PTD_DIR_MSK)
#define PTD_GET_B5_5(p)		(((p)->len & PTD_B5_5_MSK) >> 13)
#define PTD_B5_5(v)		(((v) << 13) & PTD_B5_5_MSK)
#define PTD_GET_FA(p)		(((p)->faddr & PTD_FA_MSK) >> 0)
#define PTD_FA(v)		(((v) << 0) & PTD_FA_MSK)
#define PTD_GET_FMT(p)		(((p)->faddr & PTD_FMT_MSK) >> 7)
#define PTD_FMT(v)		(((v) << 7) & PTD_FMT_MSK)

/*  Hardware transfer status codes -- CC from ptd->count */
#define TD_CC_NOERROR      0x00
#define TD_CC_CRC          0x01
#define TD_CC_BITSTUFFING  0x02
#define TD_CC_DATATOGGLEM  0x03
#define TD_CC_STALL        0x04
#define TD_DEVNOTRESP      0x05
#define TD_PIDCHECKFAIL    0x06
#define TD_UNEXPECTEDPID   0x07
#define TD_DATAOVERRUN     0x08
#define TD_DATAUNDERRUN    0x09
    /* 0x0A, 0x0B reserved for hardware */
#define TD_BUFFEROVERRUN   0x0C
#define TD_BUFFERUNDERRUN  0x0D
    /* 0x0E, 0x0F reserved for HCD */
#define TD_NOTACCESSED     0x0F

/* map PTD status codes (CC) to errno values */
static const int cc_to_error[16] = {
	/* No  Error  */ 0,
	/* CRC Error  */ -EILSEQ,
	/* Bit Stuff  */ -EPROTO,
	/* Data Togg  */ -EILSEQ,
	/* Stall      */ -EPIPE,
	/* DevNotResp */ -ETIME,
	/* PIDCheck   */ -EPROTO,
	/* UnExpPID   */ -EPROTO,
	/* DataOver   */ -EOVERFLOW,
	/* DataUnder  */ -EREMOTEIO,
	/* (for hw)   */ -EIO,
	/* (for hw)   */ -EIO,
	/* BufferOver */ -ECOMM,
	/* BuffUnder  */ -ENOSR,
	/* (for HCD)  */ -EALREADY,
	/* (for HCD)  */ -EALREADY
};

/*--------------------------------------------------------------*/

#define	LOG2_PERIODIC_SIZE	5	/* arbitrary; this matches OHCI */
#define	PERIODIC_SIZE		(1 << LOG2_PERIODIC_SIZE)

struct isp116x {
	spinlock_t lock;

	void __iomem *addr_reg;
	void __iomem *data_reg;

	struct isp116x_platform_data *board;

	struct dentry *dentry;
	unsigned long stat1, stat2, stat4, stat8, stat16;

	/* HC registers */
	u32 intenb;		/* "OHCI" interrupts */
	u16 irqenb;		/* uP interrupts */

	/* Root hub registers */
	u32 rhdesca;
	u32 rhdescb;
	u32 rhstatus;

	/* async schedule: control, bulk */
	struct list_head async;

	/* periodic schedule: int */
	u16 load[PERIODIC_SIZE];
	struct isp116x_ep *periodic[PERIODIC_SIZE];
	unsigned periodic_count;
	u16 fmindex;

	/* Schedule for the current frame */
	struct isp116x_ep *atl_active;
	int atl_buflen;
	int atl_bufshrt;
	int atl_last_dir;
	atomic_t atl_finishing;
};

static inline struct isp116x *hcd_to_isp116x(struct usb_hcd *hcd)
{
	return (struct isp116x *)(hcd->hcd_priv);
}

static inline struct usb_hcd *isp116x_to_hcd(struct isp116x *isp116x)
{
	return container_of((void *)isp116x, struct usb_hcd, hcd_priv);
}

struct isp116x_ep {
	struct usb_host_endpoint *hep;
	struct usb_device *udev;
	struct ptd ptd;

	u8 maxpacket;
	u8 epnum;
	u8 nextpid;
	u16 error_count;
	u16 length;		/* of current packet */
	unsigned char *data;	/* to databuf */
	/* queue of active EP's (the ones scheduled for the
	   current frame) */
	struct isp116x_ep *active;

	/* periodic schedule */
	u16 period;
	u16 branch;
	u16 load;
	struct isp116x_ep *next;

	/* async schedule */
	struct list_head schedule;
};

/*-------------------------------------------------------------------------*/

#define DBG(stuff...)		pr_debug("116x: " stuff)

#ifdef VERBOSE
#    define VDBG		DBG
#else
#    define VDBG(stuff...)	do{}while(0)
#endif

#define ERR(stuff...)		printk(KERN_ERR "116x: " stuff)
#define WARNING(stuff...)	printk(KERN_WARNING "116x: " stuff)
#define INFO(stuff...)		printk(KERN_INFO "116x: " stuff)

/* ------------------------------------------------- */

#if defined(USE_PLATFORM_DELAY)
#if defined(USE_NDELAY)
#error USE_PLATFORM_DELAY and USE_NDELAY simultaneously defined.
#endif
#define	isp116x_delay(h,d)	(h)->board->delay(	\
				isp116x_to_hcd(h)->self.controller,d)
#define isp116x_check_platform_delay(h)	((h)->board->delay == NULL)
#elif defined(USE_NDELAY)
#define	isp116x_delay(h,d)	ndelay(d)
#define isp116x_check_platform_delay(h)	0
#else
#define	isp116x_delay(h,d)	do{}while(0)
#define isp116x_check_platform_delay(h)	0
#endif

static inline void isp116x_write_addr(struct isp116x *isp116x, unsigned reg)
{
	writew(reg & 0xff, isp116x->addr_reg);
	isp116x_delay(isp116x, 300);
}

static inline void isp116x_write_data16(struct isp116x *isp116x, u16 val)
{
	writew(val, isp116x->data_reg);
	isp116x_delay(isp116x, 150);
}

static inline void isp116x_raw_write_data16(struct isp116x *isp116x, u16 val)
{
	__raw_writew(val, isp116x->data_reg);
	isp116x_delay(isp116x, 150);
}

static inline u16 isp116x_read_data16(struct isp116x *isp116x)
{
	u16 val;

	val = readw(isp116x->data_reg);
	isp116x_delay(isp116x, 150);
	return val;
}

static inline u16 isp116x_raw_read_data16(struct isp116x *isp116x)
{
	u16 val;

	val = __raw_readw(isp116x->data_reg);
	isp116x_delay(isp116x, 150);
	return val;
}

static inline void isp116x_write_data32(struct isp116x *isp116x, u32 val)
{
	writew(val & 0xffff, isp116x->data_reg);
	isp116x_delay(isp116x, 150);
	writew(val >> 16, isp116x->data_reg);
	isp116x_delay(isp116x, 150);
}

static inline u32 isp116x_read_data32(struct isp116x *isp116x)
{
	u32 val;

	val = (u32) readw(isp116x->data_reg);
	isp116x_delay(isp116x, 150);
	val |= ((u32) readw(isp116x->data_reg)) << 16;
	isp116x_delay(isp116x, 150);
	return val;
}

/* Let's keep register access functions out of line. Hint:
   we wait at least 150 ns at every access.
*/
static u16 isp116x_read_reg16(struct isp116x *isp116x, unsigned reg)
{
	isp116x_write_addr(isp116x, reg);
	return isp116x_read_data16(isp116x);
}

static u32 isp116x_read_reg32(struct isp116x *isp116x, unsigned reg)
{
	isp116x_write_addr(isp116x, reg);
	return isp116x_read_data32(isp116x);
}

static void isp116x_write_reg16(struct isp116x *isp116x, unsigned reg,
				unsigned val)
{
	isp116x_write_addr(isp116x, reg | ISP116x_WRITE_OFFSET);
	isp116x_write_data16(isp116x, (u16) (val & 0xffff));
}

static void isp116x_write_reg32(struct isp116x *isp116x, unsigned reg,
				unsigned val)
{
	isp116x_write_addr(isp116x, reg | ISP116x_WRITE_OFFSET);
	isp116x_write_data32(isp116x, (u32) val);
}

#define isp116x_show_reg_log(d,r,s) {				\
	if ((r) < 0x20) {			                \
		DBG("%-12s[%02x]: %08x\n", #r,			\
			r, isp116x_read_reg32(d, r));		\
	} else {						\
		DBG("%-12s[%02x]:     %04x\n", #r,		\
			r, isp116x_read_reg16(d, r));	    	\
	}							\
}
#define isp116x_show_reg_seq(d,r,s) {				\
	if ((r) < 0x20) {					\
		seq_printf(s, "%-12s[%02x]: %08x\n", #r,	\
			r, isp116x_read_reg32(d, r));		\
	} else {						\
		seq_printf(s, "%-12s[%02x]:     %04x\n", #r,	\
			r, isp116x_read_reg16(d, r));		\
	}							\
}

#define isp116x_show_regs(d,type,s) {			\
	isp116x_show_reg_##type(d, HCREVISION, s);	\
	isp116x_show_reg_##type(d, HCCONTROL, s);	\
	isp116x_show_reg_##type(d, HCCMDSTAT, s);	\
	isp116x_show_reg_##type(d, HCINTSTAT, s);	\
	isp116x_show_reg_##type(d, HCINTENB, s);	\
	isp116x_show_reg_##type(d, HCFMINTVL, s);	\
	isp116x_show_reg_##type(d, HCFMREM, s);		\
	isp116x_show_reg_##type(d, HCFMNUM, s);		\
	isp116x_show_reg_##type(d, HCLSTHRESH, s);	\
	isp116x_show_reg_##type(d, HCRHDESCA, s);	\
	isp116x_show_reg_##type(d, HCRHDESCB, s);	\
	isp116x_show_reg_##type(d, HCRHSTATUS, s);	\
	isp116x_show_reg_##type(d, HCRHPORT1, s);	\
	isp116x_show_reg_##type(d, HCRHPORT2, s);	\
	isp116x_show_reg_##type(d, HCHWCFG, s);		\
	isp116x_show_reg_##type(d, HCDMACFG, s);	\
	isp116x_show_reg_##type(d, HCXFERCTR, s);	\
	isp116x_show_reg_##type(d, HCuPINT, s);		\
	isp116x_show_reg_##type(d, HCuPINTENB, s);	\
	isp116x_show_reg_##type(d, HCCHIPID, s);	\
	isp116x_show_reg_##type(d, HCSCRATCH, s);	\
	isp116x_show_reg_##type(d, HCITLBUFLEN, s);	\
	isp116x_show_reg_##type(d, HCATLBUFLEN, s);	\
	isp116x_show_reg_##type(d, HCBUFSTAT, s);	\
	isp116x_show_reg_##type(d, HCRDITL0LEN, s);	\
	isp116x_show_reg_##type(d, HCRDITL1LEN, s);	\
}

/*
   Dump registers for debugfs.
*/
static inline void isp116x_show_regs_seq(struct isp116x *isp116x,
					  struct seq_file *s)
{
	isp116x_show_regs(isp116x, seq, s);
}

/*
   Dump registers to syslog.
*/
static inline void isp116x_show_regs_log(struct isp116x *isp116x)
{
	isp116x_show_regs(isp116x, log, NULL);
}

#if defined(URB_TRACE)

#define PIPETYPE(pipe)  ({ char *__s;			\
	if (usb_pipecontrol(pipe))	__s = "ctrl";	\
	else if (usb_pipeint(pipe))	__s = "int";	\
	else if (usb_pipebulk(pipe))	__s = "bulk";	\
	else				__s = "iso";	\
	__s;})
#define PIPEDIR(pipe)   ({ usb_pipein(pipe) ? "in" : "out"; })
#define URB_NOTSHORT(urb) ({ (urb)->transfer_flags & URB_SHORT_NOT_OK ? \
	"short_not_ok" : ""; })

/* print debug info about the URB */
static void urb_dbg(struct urb *urb, char *msg)
{
	unsigned int pipe;

	if (!urb) {
		DBG("%s: zero urb\n", msg);
		return;
	}
	pipe = urb->pipe;
	DBG("%s: FA %d ep%d%s %s: len %d/%d %s\n", msg,
	    usb_pipedevice(pipe), usb_pipeendpoint(pipe),
	    PIPEDIR(pipe), PIPETYPE(pipe),
	    urb->transfer_buffer_length, urb->actual_length, URB_NOTSHORT(urb));
}

#else

#define  urb_dbg(urb,msg)   do{}while(0)

#endif				/* ! defined(URB_TRACE) */

#if defined(PTD_TRACE)

#define PTD_DIR_STR(ptd)  ({char __c;		\
	switch(PTD_GET_DIR(ptd)){		\
	case 0:  __c = 's'; break;		\
	case 1:  __c = 'o'; break;		\
	default: __c = 'i'; break;		\
	}; __c;})

/*
  Dump PTD info. The code documents the format
  perfectly, right :)
*/
static inline void dump_ptd(struct ptd *ptd)
{
	printk(KERN_WARNING "td: %x %d%c%d %d,%d,%d  %x %x%x%x\n",
	       PTD_GET_CC(ptd), PTD_GET_FA(ptd),
	       PTD_DIR_STR(ptd), PTD_GET_EP(ptd),
	       PTD_GET_COUNT(ptd), PTD_GET_LEN(ptd), PTD_GET_MPS(ptd),
	       PTD_GET_TOGGLE(ptd), PTD_GET_ACTIVE(ptd),
	       PTD_GET_SPD(ptd), PTD_GET_LAST(ptd));
}

static inline void dump_ptd_out_data(struct ptd *ptd, u8 * buf)
{
	int k;

	if (PTD_GET_DIR(ptd) != PTD_DIR_IN && PTD_GET_LEN(ptd)) {
		printk(KERN_WARNING "-> ");
		for (k = 0; k < PTD_GET_LEN(ptd); ++k)
			printk("%02x ", ((u8 *) buf)[k]);
		printk("\n");
	}
}

static inline void dump_ptd_in_data(struct ptd *ptd, u8 * buf)
{
	int k;

	if (PTD_GET_DIR(ptd) == PTD_DIR_IN && PTD_GET_COUNT(ptd)) {
		printk(KERN_WARNING "<- ");
		for (k = 0; k < PTD_GET_COUNT(ptd); ++k)
			printk("%02x ", ((u8 *) buf)[k]);
		printk("\n");
	}
	if (PTD_GET_LAST(ptd))
		printk(KERN_WARNING "-\n");
}

#else

#define dump_ptd(ptd)               do{}while(0)
#define dump_ptd_in_data(ptd,buf)   do{}while(0)
#define dump_ptd_out_data(ptd,buf)  do{}while(0)

#endif				/* ! defined(PTD_TRACE) */
