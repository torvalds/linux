/*
 * omap_udc.h -- for omap 3.2 udc, with OTG support
 *
 * 2004 (C) Texas Instruments, Inc.
 * 2004 (C) David Brownell
 */

/*
 * USB device/endpoint management registers
 */
#define UDC_REG(offset)              __REG16(UDC_BASE + (offset))

#define	UDC_REV_REG			UDC_REG(0x0)	/* Revision */
#define	UDC_EP_NUM_REG			UDC_REG(0x4)	/* Which endpoint */
#	define	UDC_SETUP_SEL		(1 << 6)
#	define	UDC_EP_SEL		(1 << 5)
#	define	UDC_EP_DIR		(1 << 4)
	/* low 4 bits for endpoint number */
#define	UDC_DATA_REG			UDC_REG(0x08)	/* Endpoint FIFO */
#define	UDC_CTRL_REG			UDC_REG(0x0C)	/* Endpoint control */
#	define	UDC_CLR_HALT		(1 << 7)
#	define	UDC_SET_HALT		(1 << 6)
#	define	UDC_CLRDATA_TOGGLE	(1 << 3)
#	define	UDC_SET_FIFO_EN		(1 << 2)
#	define	UDC_CLR_EP		(1 << 1)
#	define	UDC_RESET_EP		(1 << 0)
#define	UDC_STAT_FLG_REG		UDC_REG(0x10)	/* Endpoint status */
#	define	UDC_NO_RXPACKET		(1 << 15)
#	define	UDC_MISS_IN		(1 << 14)
#	define	UDC_DATA_FLUSH		(1 << 13)
#	define	UDC_ISO_ERR		(1 << 12)
#	define	UDC_ISO_FIFO_EMPTY	(1 << 9)
#	define	UDC_ISO_FIFO_FULL	(1 << 8)
#	define	UDC_EP_HALTED		(1 << 6)
#	define	UDC_STALL		(1 << 5)
#	define	UDC_NAK			(1 << 4)
#	define	UDC_ACK			(1 << 3)
#	define	UDC_FIFO_EN		(1 << 2)
#	define	UDC_NON_ISO_FIFO_EMPTY	(1 << 1)
#	define	UDC_NON_ISO_FIFO_FULL	(1 << 0)
#define	UDC_RXFSTAT_REG			UDC_REG(0x14)	/* OUT bytecount */
#define	UDC_SYSCON1_REG			UDC_REG(0x18)	/* System config 1 */
#	define	UDC_CFG_LOCK		(1 << 8)
#	define	UDC_DATA_ENDIAN		(1 << 7)
#	define	UDC_DMA_ENDIAN		(1 << 6)
#	define	UDC_NAK_EN		(1 << 4)
#	define	UDC_AUTODECODE_DIS	(1 << 3)
#	define	UDC_SELF_PWR		(1 << 2)
#	define	UDC_SOFF_DIS		(1 << 1)
#	define	UDC_PULLUP_EN		(1 << 0)
#define	UDC_SYSCON2_REG			UDC_REG(0x1C)	/* System config 2 */
#	define	UDC_RMT_WKP		(1 << 6)
#	define	UDC_STALL_CMD		(1 << 5)
#	define	UDC_DEV_CFG		(1 << 3)
#	define	UDC_CLR_CFG		(1 << 2)
#define	UDC_DEVSTAT_REG			UDC_REG(0x20)	/* Device status */
#	define	UDC_B_HNP_ENABLE	(1 << 9)
#	define	UDC_A_HNP_SUPPORT	(1 << 8)
#	define	UDC_A_ALT_HNP_SUPPORT	(1 << 7)
#	define	UDC_R_WK_OK		(1 << 6)
#	define	UDC_USB_RESET		(1 << 5)
#	define	UDC_SUS			(1 << 4)
#	define	UDC_CFG			(1 << 3)
#	define	UDC_ADD			(1 << 2)
#	define	UDC_DEF			(1 << 1)
#	define	UDC_ATT			(1 << 0)
#define	UDC_SOF_REG			UDC_REG(0x24)	/* Start of frame */
#	define	UDC_FT_LOCK		(1 << 12)
#	define	UDC_TS_OK		(1 << 11)
#	define	UDC_TS			0x03ff
#define	UDC_IRQ_EN_REG			UDC_REG(0x28)	/* Interrupt enable */
#	define	UDC_SOF_IE		(1 << 7)
#	define	UDC_EPN_RX_IE		(1 << 5)
#	define	UDC_EPN_TX_IE		(1 << 4)
#	define	UDC_DS_CHG_IE		(1 << 3)
#	define	UDC_EP0_IE		(1 << 0)
#define	UDC_DMA_IRQ_EN_REG		UDC_REG(0x2C)	/* DMA irq enable */
	/* rx/tx dma channels numbered 1-3 not 0-2 */
#	define	UDC_TX_DONE_IE(n)	(1 << (4 * (n) - 2))
#	define	UDC_RX_CNT_IE(n)	(1 << (4 * (n) - 3))
#	define	UDC_RX_EOT_IE(n)	(1 << (4 * (n) - 4))
#define	UDC_IRQ_SRC_REG			UDC_REG(0x30)	/* Interrupt source */
#	define	UDC_TXN_DONE		(1 << 10)
#	define	UDC_RXN_CNT		(1 << 9)
#	define	UDC_RXN_EOT		(1 << 8)
#	define	UDC_SOF			(1 << 7)
#	define	UDC_EPN_RX		(1 << 5)
#	define	UDC_EPN_TX		(1 << 4)
#	define	UDC_DS_CHG		(1 << 3)
#	define	UDC_SETUP		(1 << 2)
#	define	UDC_EP0_RX		(1 << 1)
#	define	UDC_EP0_TX		(1 << 0)
#	define	UDC_IRQ_SRC_MASK	0x7bf
#define	UDC_EPN_STAT_REG		UDC_REG(0x34)	/* EP irq status */
#define	UDC_DMAN_STAT_REG		UDC_REG(0x38)	/* DMA irq status */
#	define	UDC_DMA_RX_SB		(1 << 12)
#	define	UDC_DMA_RX_SRC(x)	(((x)>>8) & 0xf)
#	define	UDC_DMA_TX_SRC(x)	(((x)>>0) & 0xf)


/* DMA configuration registers:  up to three channels in each direction.  */
#define	UDC_RXDMA_CFG_REG		UDC_REG(0x40)	/* 3 eps for RX DMA */
#	define	UDC_DMA_REQ		(1 << 12)
#define	UDC_TXDMA_CFG_REG		UDC_REG(0x44)	/* 3 eps for TX DMA */
#define	UDC_DATA_DMA_REG		UDC_REG(0x48)	/* rx/tx fifo addr */

/* rx/tx dma control, numbering channels 1-3 not 0-2 */
#define	UDC_TXDMA_REG(chan)		UDC_REG(0x50 - 4 + 4 * (chan))
#	define UDC_TXN_EOT		(1 << 15)	/* bytes vs packets */
#	define UDC_TXN_START		(1 << 14)	/* start transfer */
#	define UDC_TXN_TSC		0x03ff		/* units in xfer */
#define	UDC_RXDMA_REG(chan)		UDC_REG(0x60 - 4 + 4 * (chan))
#	define UDC_RXN_STOP		(1 << 15)	/* enable EOT irq */
#	define UDC_RXN_TC		0x00ff		/* packets in xfer */


/*
 * Endpoint configuration registers (used before CFG_LOCK is set)
 * UDC_EP_TX_REG(0) is unused
 */
#define	UDC_EP_RX_REG(endpoint)		UDC_REG(0x80 + (endpoint)*4)
#	define	UDC_EPN_RX_VALID	(1 << 15)
#	define	UDC_EPN_RX_DB		(1 << 14)
	/* buffer size in bits 13, 12 */
#	define	UDC_EPN_RX_ISO		(1 << 11)
	/* buffer pointer in low 11 bits */
#define	UDC_EP_TX_REG(endpoint)		UDC_REG(0xc0 + (endpoint)*4)
	/* same bitfields as in RX_REG */

/*-------------------------------------------------------------------------*/

struct omap_req {
	struct usb_request		req;
	struct list_head		queue;
	unsigned			dma_bytes;
	unsigned			mapped:1;
};

struct omap_ep {
	struct usb_ep			ep;
	struct list_head		queue;
	unsigned long			irqs;
	struct list_head		iso;
	const struct usb_endpoint_descriptor	*desc;
	char				name[14];
	u16				maxpacket;
	u8				bEndpointAddress;
	u8				bmAttributes;
	unsigned			double_buf:1;
	unsigned			stopped:1;
	unsigned			fnf:1;
	unsigned			has_dma:1;
	u8				ackwait;
	u8				dma_channel;
	u16				dma_counter;
	int				lch;
	struct omap_udc			*udc;
	struct timer_list		timer;
};

struct omap_udc {
	struct usb_gadget		gadget;
	struct usb_gadget_driver	*driver;
	spinlock_t			lock;
	struct omap_ep			ep[32];
	u16				devstat;
	u16				clr_halt;
	struct otg_transceiver		*transceiver;
	struct list_head		iso;
	unsigned			softconnect:1;
	unsigned			vbus_active:1;
	unsigned			ep0_pending:1;
	unsigned			ep0_in:1;
	unsigned			ep0_set_config:1;
	unsigned			ep0_reset_config:1;
	unsigned			ep0_setup:1;
	struct completion		*done;
	struct clk			*dc_clk;
	struct clk			*hhc_clk;
	unsigned			clk_requested:1;
};

/*-------------------------------------------------------------------------*/

#ifdef VERBOSE
#    define VDBG		DBG
#else
#    define VDBG(stuff...)	do{}while(0)
#endif

#define ERR(stuff...)		pr_err("udc: " stuff)
#define WARN(stuff...)		pr_warning("udc: " stuff)
#define INFO(stuff...)		pr_info("udc: " stuff)
#define DBG(stuff...)		pr_debug("udc: " stuff)

/*-------------------------------------------------------------------------*/

#define	MOD_CONF_CTRL_0_REG	__REG32(MOD_CONF_CTRL_0)
#define	VBUS_W2FC_1510		(1 << 17)	/* 0 gpio0, 1 dvdd2 pin */

#define	FUNC_MUX_CTRL_0_REG	__REG32(FUNC_MUX_CTRL_0)
#define	VBUS_CTRL_1510		(1 << 19)	/* 1 connected (software) */
#define	VBUS_MODE_1510		(1 << 18)	/* 0 hardware, 1 software */

#define	HMC_1510	((MOD_CONF_CTRL_0_REG >> 1) & 0x3f)
#define	HMC_1610	(OTG_SYSCON_2_REG & 0x3f)
#define	HMC		(cpu_is_omap15xx() ? HMC_1510 : HMC_1610)

