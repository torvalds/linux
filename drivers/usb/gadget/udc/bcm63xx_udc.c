// SPDX-License-Identifier: GPL-2.0+
/*
 * bcm63xx_udc.c -- BCM63xx UDC high/full speed USB device controller
 *
 * Copyright (C) 2012 Kevin Cernekee <cernekee@gmail.com>
 * Copyright (C) 2012 Broadcom Corporation
 */

#include <linux/bitops.h>
#include <linux/bug.h>
#include <linux/clk.h>
#include <linux/compiler.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/usb.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <linux/workqueue.h>

#include <bcm63xx_cpu.h>
#include <bcm63xx_iudma.h>
#include <bcm63xx_dev_usb_usbd.h>
#include <bcm63xx_io.h>
#include <bcm63xx_regs.h>

#define DRV_MODULE_NAME		"bcm63xx_udc"

static const char bcm63xx_ep0name[] = "ep0";

static const struct {
	const char *name;
	const struct usb_ep_caps caps;
} bcm63xx_ep_info[] = {
#define EP_INFO(_name, _caps) \
	{ \
		.name = _name, \
		.caps = _caps, \
	}

	EP_INFO(bcm63xx_ep0name,
		USB_EP_CAPS(USB_EP_CAPS_TYPE_CONTROL, USB_EP_CAPS_DIR_ALL)),
	EP_INFO("ep1in-bulk",
		USB_EP_CAPS(USB_EP_CAPS_TYPE_BULK, USB_EP_CAPS_DIR_IN)),
	EP_INFO("ep2out-bulk",
		USB_EP_CAPS(USB_EP_CAPS_TYPE_BULK, USB_EP_CAPS_DIR_OUT)),
	EP_INFO("ep3in-int",
		USB_EP_CAPS(USB_EP_CAPS_TYPE_INT, USB_EP_CAPS_DIR_IN)),
	EP_INFO("ep4out-int",
		USB_EP_CAPS(USB_EP_CAPS_TYPE_INT, USB_EP_CAPS_DIR_OUT)),

#undef EP_INFO
};

static bool use_fullspeed;
module_param(use_fullspeed, bool, S_IRUGO);
MODULE_PARM_DESC(use_fullspeed, "true for fullspeed only");

/*
 * RX IRQ coalescing options:
 *
 * false (default) - one IRQ per DATAx packet.  Slow but reliable.  The
 * driver is able to pass the "testusb" suite and recover from conditions like:
 *
 *   1) Device queues up a 2048-byte RX IUDMA transaction on an OUT bulk ep
 *   2) Host sends 512 bytes of data
 *   3) Host decides to reconfigure the device and sends SET_INTERFACE
 *   4) Device shuts down the endpoint and cancels the RX transaction
 *
 * true - one IRQ per transfer, for transfers <= 2048B.  Generates
 * considerably fewer IRQs, but error recovery is less robust.  Does not
 * reliably pass "testusb".
 *
 * TX always uses coalescing, because we can cancel partially complete TX
 * transfers by repeatedly flushing the FIFO.  The hardware doesn't allow
 * this on RX.
 */
static bool irq_coalesce;
module_param(irq_coalesce, bool, S_IRUGO);
MODULE_PARM_DESC(irq_coalesce, "take one IRQ per RX transfer");

#define BCM63XX_NUM_EP			5
#define BCM63XX_NUM_IUDMA		6
#define BCM63XX_NUM_FIFO_PAIRS		3

#define IUDMA_RESET_TIMEOUT_US		10000

#define IUDMA_EP0_RXCHAN		0
#define IUDMA_EP0_TXCHAN		1

#define IUDMA_MAX_FRAGMENT		2048
#define BCM63XX_MAX_CTRL_PKT		64

#define BCMEP_CTRL			0x00
#define BCMEP_ISOC			0x01
#define BCMEP_BULK			0x02
#define BCMEP_INTR			0x03

#define BCMEP_OUT			0x00
#define BCMEP_IN			0x01

#define BCM63XX_SPD_FULL		1
#define BCM63XX_SPD_HIGH		0

#define IUDMA_DMAC_OFFSET		0x200
#define IUDMA_DMAS_OFFSET		0x400

enum bcm63xx_ep0_state {
	EP0_REQUEUE,
	EP0_IDLE,
	EP0_IN_DATA_PHASE_SETUP,
	EP0_IN_DATA_PHASE_COMPLETE,
	EP0_OUT_DATA_PHASE_SETUP,
	EP0_OUT_DATA_PHASE_COMPLETE,
	EP0_OUT_STATUS_PHASE,
	EP0_IN_FAKE_STATUS_PHASE,
	EP0_SHUTDOWN,
};

static const char __maybe_unused bcm63xx_ep0_state_names[][32] = {
	"REQUEUE",
	"IDLE",
	"IN_DATA_PHASE_SETUP",
	"IN_DATA_PHASE_COMPLETE",
	"OUT_DATA_PHASE_SETUP",
	"OUT_DATA_PHASE_COMPLETE",
	"OUT_STATUS_PHASE",
	"IN_FAKE_STATUS_PHASE",
	"SHUTDOWN",
};

/**
 * struct iudma_ch_cfg - Static configuration for an IUDMA channel.
 * @ep_num: USB endpoint number.
 * @n_bds: Number of buffer descriptors in the ring.
 * @ep_type: Endpoint type (control, bulk, interrupt).
 * @dir: Direction (in, out).
 * @n_fifo_slots: Number of FIFO entries to allocate for this channel.
 * @max_pkt_hs: Maximum packet size in high speed mode.
 * @max_pkt_fs: Maximum packet size in full speed mode.
 */
struct iudma_ch_cfg {
	int				ep_num;
	int				n_bds;
	int				ep_type;
	int				dir;
	int				n_fifo_slots;
	int				max_pkt_hs;
	int				max_pkt_fs;
};

static const struct iudma_ch_cfg iudma_defaults[] = {

	/* This controller was designed to support a CDC/RNDIS application.
	   It may be possible to reconfigure some of the endpoints, but
	   the hardware limitations (FIFO sizing and number of DMA channels)
	   may significantly impact flexibility and/or stability.  Change
	   these values at your own risk.

	      ep_num       ep_type           n_fifo_slots    max_pkt_fs
	idx      |  n_bds     |         dir       |  max_pkt_hs  |
	 |       |    |       |          |        |      |       |       */
	[0] = { -1,   4, BCMEP_CTRL, BCMEP_OUT,  32,    64,     64 },
	[1] = {  0,   4, BCMEP_CTRL, BCMEP_OUT,  32,    64,     64 },
	[2] = {  2,  16, BCMEP_BULK, BCMEP_OUT, 128,   512,     64 },
	[3] = {  1,  16, BCMEP_BULK, BCMEP_IN,  128,   512,     64 },
	[4] = {  4,   4, BCMEP_INTR, BCMEP_OUT,  32,    64,     64 },
	[5] = {  3,   4, BCMEP_INTR, BCMEP_IN,   32,    64,     64 },
};

struct bcm63xx_udc;

/**
 * struct iudma_ch - Represents the current state of a single IUDMA channel.
 * @ch_idx: IUDMA channel index (0 to BCM63XX_NUM_IUDMA-1).
 * @ep_num: USB endpoint number.  -1 for ep0 RX.
 * @enabled: Whether bcm63xx_ep_enable() has been called.
 * @max_pkt: "Chunk size" on the USB interface.  Based on interface speed.
 * @is_tx: true for TX, false for RX.
 * @bep: Pointer to the associated endpoint.  NULL for ep0 RX.
 * @udc: Reference to the device controller.
 * @read_bd: Next buffer descriptor to reap from the hardware.
 * @write_bd: Next BD available for a new packet.
 * @end_bd: Points to the final BD in the ring.
 * @n_bds_used: Number of BD entries currently occupied.
 * @bd_ring: Base pointer to the BD ring.
 * @bd_ring_dma: Physical (DMA) address of bd_ring.
 * @n_bds: Total number of BDs in the ring.
 *
 * ep0 has two IUDMA channels (IUDMA_EP0_RXCHAN and IUDMA_EP0_TXCHAN), as it is
 * bidirectional.  The "struct usb_ep" associated with ep0 is for TX (IN)
 * only.
 *
 * Each bulk/intr endpoint has a single IUDMA channel and a single
 * struct usb_ep.
 */
struct iudma_ch {
	unsigned int			ch_idx;
	int				ep_num;
	bool				enabled;
	int				max_pkt;
	bool				is_tx;
	struct bcm63xx_ep		*bep;
	struct bcm63xx_udc		*udc;

	struct bcm_enet_desc		*read_bd;
	struct bcm_enet_desc		*write_bd;
	struct bcm_enet_desc		*end_bd;
	int				n_bds_used;

	struct bcm_enet_desc		*bd_ring;
	dma_addr_t			bd_ring_dma;
	unsigned int			n_bds;
};

/**
 * struct bcm63xx_ep - Internal (driver) state of a single endpoint.
 * @ep_num: USB endpoint number.
 * @iudma: Pointer to IUDMA channel state.
 * @ep: USB gadget layer representation of the EP.
 * @udc: Reference to the device controller.
 * @queue: Linked list of outstanding requests for this EP.
 * @halted: 1 if the EP is stalled; 0 otherwise.
 */
struct bcm63xx_ep {
	unsigned int			ep_num;
	struct iudma_ch			*iudma;
	struct usb_ep			ep;
	struct bcm63xx_udc		*udc;
	struct list_head		queue;
	unsigned			halted:1;
};

/**
 * struct bcm63xx_req - Internal (driver) state of a single request.
 * @queue: Links back to the EP's request list.
 * @req: USB gadget layer representation of the request.
 * @offset: Current byte offset into the data buffer (next byte to queue).
 * @bd_bytes: Number of data bytes in outstanding BD entries.
 * @iudma: IUDMA channel used for the request.
 */
struct bcm63xx_req {
	struct list_head		queue;		/* ep's requests */
	struct usb_request		req;
	unsigned int			offset;
	unsigned int			bd_bytes;
	struct iudma_ch			*iudma;
};

/**
 * struct bcm63xx_udc - Driver/hardware private context.
 * @lock: Spinlock to mediate access to this struct, and (most) HW regs.
 * @dev: Generic Linux device structure.
 * @pd: Platform data (board/port info).
 * @usbd_clk: Clock descriptor for the USB device block.
 * @usbh_clk: Clock descriptor for the USB host block.
 * @gadget: USB device.
 * @driver: Driver for USB device.
 * @usbd_regs: Base address of the USBD/USB20D block.
 * @iudma_regs: Base address of the USBD's associated IUDMA block.
 * @bep: Array of endpoints, including ep0.
 * @iudma: Array of all IUDMA channels used by this controller.
 * @cfg: USB configuration number, from SET_CONFIGURATION wValue.
 * @iface: USB interface number, from SET_INTERFACE wIndex.
 * @alt_iface: USB alt interface number, from SET_INTERFACE wValue.
 * @ep0_ctrl_req: Request object for bcm63xx_udc-initiated ep0 transactions.
 * @ep0_ctrl_buf: Data buffer for ep0_ctrl_req.
 * @ep0state: Current state of the ep0 state machine.
 * @ep0_wq: Workqueue struct used to wake up the ep0 state machine.
 * @wedgemap: Bitmap of wedged endpoints.
 * @ep0_req_reset: USB reset is pending.
 * @ep0_req_set_cfg: Need to spoof a SET_CONFIGURATION packet.
 * @ep0_req_set_iface: Need to spoof a SET_INTERFACE packet.
 * @ep0_req_shutdown: Driver is shutting down; requesting ep0 to halt activity.
 * @ep0_req_completed: ep0 request has completed; worker has not seen it yet.
 * @ep0_reply: Pending reply from gadget driver.
 * @ep0_request: Outstanding ep0 request.
 */
struct bcm63xx_udc {
	spinlock_t			lock;

	struct device			*dev;
	struct bcm63xx_usbd_platform_data *pd;
	struct clk			*usbd_clk;
	struct clk			*usbh_clk;

	struct usb_gadget		gadget;
	struct usb_gadget_driver	*driver;

	void __iomem			*usbd_regs;
	void __iomem			*iudma_regs;

	struct bcm63xx_ep		bep[BCM63XX_NUM_EP];
	struct iudma_ch			iudma[BCM63XX_NUM_IUDMA];

	int				cfg;
	int				iface;
	int				alt_iface;

	struct bcm63xx_req		ep0_ctrl_req;
	u8				*ep0_ctrl_buf;

	int				ep0state;
	struct work_struct		ep0_wq;

	unsigned long			wedgemap;

	unsigned			ep0_req_reset:1;
	unsigned			ep0_req_set_cfg:1;
	unsigned			ep0_req_set_iface:1;
	unsigned			ep0_req_shutdown:1;

	unsigned			ep0_req_completed:1;
	struct usb_request		*ep0_reply;
	struct usb_request		*ep0_request;
};

static const struct usb_ep_ops bcm63xx_udc_ep_ops;

/***********************************************************************
 * Convenience functions
 ***********************************************************************/

static inline struct bcm63xx_udc *gadget_to_udc(struct usb_gadget *g)
{
	return container_of(g, struct bcm63xx_udc, gadget);
}

static inline struct bcm63xx_ep *our_ep(struct usb_ep *ep)
{
	return container_of(ep, struct bcm63xx_ep, ep);
}

static inline struct bcm63xx_req *our_req(struct usb_request *req)
{
	return container_of(req, struct bcm63xx_req, req);
}

static inline u32 usbd_readl(struct bcm63xx_udc *udc, u32 off)
{
	return bcm_readl(udc->usbd_regs + off);
}

static inline void usbd_writel(struct bcm63xx_udc *udc, u32 val, u32 off)
{
	bcm_writel(val, udc->usbd_regs + off);
}

static inline u32 usb_dma_readl(struct bcm63xx_udc *udc, u32 off)
{
	return bcm_readl(udc->iudma_regs + off);
}

static inline void usb_dma_writel(struct bcm63xx_udc *udc, u32 val, u32 off)
{
	bcm_writel(val, udc->iudma_regs + off);
}

static inline u32 usb_dmac_readl(struct bcm63xx_udc *udc, u32 off, int chan)
{
	return bcm_readl(udc->iudma_regs + IUDMA_DMAC_OFFSET + off +
			(ENETDMA_CHAN_WIDTH * chan));
}

static inline void usb_dmac_writel(struct bcm63xx_udc *udc, u32 val, u32 off,
					int chan)
{
	bcm_writel(val, udc->iudma_regs + IUDMA_DMAC_OFFSET + off +
			(ENETDMA_CHAN_WIDTH * chan));
}

static inline u32 usb_dmas_readl(struct bcm63xx_udc *udc, u32 off, int chan)
{
	return bcm_readl(udc->iudma_regs + IUDMA_DMAS_OFFSET + off +
			(ENETDMA_CHAN_WIDTH * chan));
}

static inline void usb_dmas_writel(struct bcm63xx_udc *udc, u32 val, u32 off,
					int chan)
{
	bcm_writel(val, udc->iudma_regs + IUDMA_DMAS_OFFSET + off +
			(ENETDMA_CHAN_WIDTH * chan));
}

static inline void set_clocks(struct bcm63xx_udc *udc, bool is_enabled)
{
	if (is_enabled) {
		clk_enable(udc->usbh_clk);
		clk_enable(udc->usbd_clk);
		udelay(10);
	} else {
		clk_disable(udc->usbd_clk);
		clk_disable(udc->usbh_clk);
	}
}

/***********************************************************************
 * Low-level IUDMA / FIFO operations
 ***********************************************************************/

/**
 * bcm63xx_ep_dma_select - Helper function to set up the init_sel signal.
 * @udc: Reference to the device controller.
 * @idx: Desired init_sel value.
 *
 * The "init_sel" signal is used as a selection index for both endpoints
 * and IUDMA channels.  Since these do not map 1:1, the use of this signal
 * depends on the context.
 */
static void bcm63xx_ep_dma_select(struct bcm63xx_udc *udc, int idx)
{
	u32 val = usbd_readl(udc, USBD_CONTROL_REG);

	val &= ~USBD_CONTROL_INIT_SEL_MASK;
	val |= idx << USBD_CONTROL_INIT_SEL_SHIFT;
	usbd_writel(udc, val, USBD_CONTROL_REG);
}

/**
 * bcm63xx_set_stall - Enable/disable stall on one endpoint.
 * @udc: Reference to the device controller.
 * @bep: Endpoint on which to operate.
 * @is_stalled: true to enable stall, false to disable.
 *
 * See notes in bcm63xx_update_wedge() regarding automatic clearing of
 * halt/stall conditions.
 */
static void bcm63xx_set_stall(struct bcm63xx_udc *udc, struct bcm63xx_ep *bep,
	bool is_stalled)
{
	u32 val;

	val = USBD_STALL_UPDATE_MASK |
		(is_stalled ? USBD_STALL_ENABLE_MASK : 0) |
		(bep->ep_num << USBD_STALL_EPNUM_SHIFT);
	usbd_writel(udc, val, USBD_STALL_REG);
}

/**
 * bcm63xx_fifo_setup - (Re)initialize FIFO boundaries and settings.
 * @udc: Reference to the device controller.
 *
 * These parameters depend on the USB link speed.  Settings are
 * per-IUDMA-channel-pair.
 */
static void bcm63xx_fifo_setup(struct bcm63xx_udc *udc)
{
	int is_hs = udc->gadget.speed == USB_SPEED_HIGH;
	u32 i, val, rx_fifo_slot, tx_fifo_slot;

	/* set up FIFO boundaries and packet sizes; this is done in pairs */
	rx_fifo_slot = tx_fifo_slot = 0;
	for (i = 0; i < BCM63XX_NUM_IUDMA; i += 2) {
		const struct iudma_ch_cfg *rx_cfg = &iudma_defaults[i];
		const struct iudma_ch_cfg *tx_cfg = &iudma_defaults[i + 1];

		bcm63xx_ep_dma_select(udc, i >> 1);

		val = (rx_fifo_slot << USBD_RXFIFO_CONFIG_START_SHIFT) |
			((rx_fifo_slot + rx_cfg->n_fifo_slots - 1) <<
			 USBD_RXFIFO_CONFIG_END_SHIFT);
		rx_fifo_slot += rx_cfg->n_fifo_slots;
		usbd_writel(udc, val, USBD_RXFIFO_CONFIG_REG);
		usbd_writel(udc,
			    is_hs ? rx_cfg->max_pkt_hs : rx_cfg->max_pkt_fs,
			    USBD_RXFIFO_EPSIZE_REG);

		val = (tx_fifo_slot << USBD_TXFIFO_CONFIG_START_SHIFT) |
			((tx_fifo_slot + tx_cfg->n_fifo_slots - 1) <<
			 USBD_TXFIFO_CONFIG_END_SHIFT);
		tx_fifo_slot += tx_cfg->n_fifo_slots;
		usbd_writel(udc, val, USBD_TXFIFO_CONFIG_REG);
		usbd_writel(udc,
			    is_hs ? tx_cfg->max_pkt_hs : tx_cfg->max_pkt_fs,
			    USBD_TXFIFO_EPSIZE_REG);

		usbd_readl(udc, USBD_TXFIFO_EPSIZE_REG);
	}
}

/**
 * bcm63xx_fifo_reset_ep - Flush a single endpoint's FIFO.
 * @udc: Reference to the device controller.
 * @ep_num: Endpoint number.
 */
static void bcm63xx_fifo_reset_ep(struct bcm63xx_udc *udc, int ep_num)
{
	u32 val;

	bcm63xx_ep_dma_select(udc, ep_num);

	val = usbd_readl(udc, USBD_CONTROL_REG);
	val |= USBD_CONTROL_FIFO_RESET_MASK;
	usbd_writel(udc, val, USBD_CONTROL_REG);
	usbd_readl(udc, USBD_CONTROL_REG);
}

/**
 * bcm63xx_fifo_reset - Flush all hardware FIFOs.
 * @udc: Reference to the device controller.
 */
static void bcm63xx_fifo_reset(struct bcm63xx_udc *udc)
{
	int i;

	for (i = 0; i < BCM63XX_NUM_FIFO_PAIRS; i++)
		bcm63xx_fifo_reset_ep(udc, i);
}

/**
 * bcm63xx_ep_init - Initial (one-time) endpoint initialization.
 * @udc: Reference to the device controller.
 */
static void bcm63xx_ep_init(struct bcm63xx_udc *udc)
{
	u32 i, val;

	for (i = 0; i < BCM63XX_NUM_IUDMA; i++) {
		const struct iudma_ch_cfg *cfg = &iudma_defaults[i];

		if (cfg->ep_num < 0)
			continue;

		bcm63xx_ep_dma_select(udc, cfg->ep_num);
		val = (cfg->ep_type << USBD_EPNUM_TYPEMAP_TYPE_SHIFT) |
			((i >> 1) << USBD_EPNUM_TYPEMAP_DMA_CH_SHIFT);
		usbd_writel(udc, val, USBD_EPNUM_TYPEMAP_REG);
	}
}

/**
 * bcm63xx_ep_setup - Configure per-endpoint settings.
 * @udc: Reference to the device controller.
 *
 * This needs to be rerun if the speed/cfg/intf/altintf changes.
 */
static void bcm63xx_ep_setup(struct bcm63xx_udc *udc)
{
	u32 val, i;

	usbd_writel(udc, USBD_CSR_SETUPADDR_DEF, USBD_CSR_SETUPADDR_REG);

	for (i = 0; i < BCM63XX_NUM_IUDMA; i++) {
		const struct iudma_ch_cfg *cfg = &iudma_defaults[i];
		int max_pkt = udc->gadget.speed == USB_SPEED_HIGH ?
			      cfg->max_pkt_hs : cfg->max_pkt_fs;
		int idx = cfg->ep_num;

		udc->iudma[i].max_pkt = max_pkt;

		if (idx < 0)
			continue;
		usb_ep_set_maxpacket_limit(&udc->bep[idx].ep, max_pkt);

		val = (idx << USBD_CSR_EP_LOG_SHIFT) |
		      (cfg->dir << USBD_CSR_EP_DIR_SHIFT) |
		      (cfg->ep_type << USBD_CSR_EP_TYPE_SHIFT) |
		      (udc->cfg << USBD_CSR_EP_CFG_SHIFT) |
		      (udc->iface << USBD_CSR_EP_IFACE_SHIFT) |
		      (udc->alt_iface << USBD_CSR_EP_ALTIFACE_SHIFT) |
		      (max_pkt << USBD_CSR_EP_MAXPKT_SHIFT);
		usbd_writel(udc, val, USBD_CSR_EP_REG(idx));
	}
}

/**
 * iudma_write - Queue a single IUDMA transaction.
 * @udc: Reference to the device controller.
 * @iudma: IUDMA channel to use.
 * @breq: Request containing the transaction data.
 *
 * For RX IUDMA, this will queue a single buffer descriptor, as RX IUDMA
 * does not honor SOP/EOP so the handling of multiple buffers is ambiguous.
 * So iudma_write() may be called several times to fulfill a single
 * usb_request.
 *
 * For TX IUDMA, this can queue multiple buffer descriptors if needed.
 */
static void iudma_write(struct bcm63xx_udc *udc, struct iudma_ch *iudma,
	struct bcm63xx_req *breq)
{
	int first_bd = 1, last_bd = 0, extra_zero_pkt = 0;
	unsigned int bytes_left = breq->req.length - breq->offset;
	const int max_bd_bytes = !irq_coalesce && !iudma->is_tx ?
		iudma->max_pkt : IUDMA_MAX_FRAGMENT;

	iudma->n_bds_used = 0;
	breq->bd_bytes = 0;
	breq->iudma = iudma;

	if ((bytes_left % iudma->max_pkt == 0) && bytes_left && breq->req.zero)
		extra_zero_pkt = 1;

	do {
		struct bcm_enet_desc *d = iudma->write_bd;
		u32 dmaflags = 0;
		unsigned int n_bytes;

		if (d == iudma->end_bd) {
			dmaflags |= DMADESC_WRAP_MASK;
			iudma->write_bd = iudma->bd_ring;
		} else {
			iudma->write_bd++;
		}
		iudma->n_bds_used++;

		n_bytes = min_t(int, bytes_left, max_bd_bytes);
		if (n_bytes)
			dmaflags |= n_bytes << DMADESC_LENGTH_SHIFT;
		else
			dmaflags |= (1 << DMADESC_LENGTH_SHIFT) |
				    DMADESC_USB_ZERO_MASK;

		dmaflags |= DMADESC_OWNER_MASK;
		if (first_bd) {
			dmaflags |= DMADESC_SOP_MASK;
			first_bd = 0;
		}

		/*
		 * extra_zero_pkt forces one more iteration through the loop
		 * after all data is queued up, to send the zero packet
		 */
		if (extra_zero_pkt && !bytes_left)
			extra_zero_pkt = 0;

		if (!iudma->is_tx || iudma->n_bds_used == iudma->n_bds ||
		    (n_bytes == bytes_left && !extra_zero_pkt)) {
			last_bd = 1;
			dmaflags |= DMADESC_EOP_MASK;
		}

		d->address = breq->req.dma + breq->offset;
		mb();
		d->len_stat = dmaflags;

		breq->offset += n_bytes;
		breq->bd_bytes += n_bytes;
		bytes_left -= n_bytes;
	} while (!last_bd);

	usb_dmac_writel(udc, ENETDMAC_CHANCFG_EN_MASK,
			ENETDMAC_CHANCFG_REG, iudma->ch_idx);
}

/**
 * iudma_read - Check for IUDMA buffer completion.
 * @udc: Reference to the device controller.
 * @iudma: IUDMA channel to use.
 *
 * This checks to see if ALL of the outstanding BDs on the DMA channel
 * have been filled.  If so, it returns the actual transfer length;
 * otherwise it returns -EBUSY.
 */
static int iudma_read(struct bcm63xx_udc *udc, struct iudma_ch *iudma)
{
	int i, actual_len = 0;
	struct bcm_enet_desc *d = iudma->read_bd;

	if (!iudma->n_bds_used)
		return -EINVAL;

	for (i = 0; i < iudma->n_bds_used; i++) {
		u32 dmaflags;

		dmaflags = d->len_stat;

		if (dmaflags & DMADESC_OWNER_MASK)
			return -EBUSY;

		actual_len += (dmaflags & DMADESC_LENGTH_MASK) >>
			      DMADESC_LENGTH_SHIFT;
		if (d == iudma->end_bd)
			d = iudma->bd_ring;
		else
			d++;
	}

	iudma->read_bd = d;
	iudma->n_bds_used = 0;
	return actual_len;
}

/**
 * iudma_reset_channel - Stop DMA on a single channel.
 * @udc: Reference to the device controller.
 * @iudma: IUDMA channel to reset.
 */
static void iudma_reset_channel(struct bcm63xx_udc *udc, struct iudma_ch *iudma)
{
	int timeout = IUDMA_RESET_TIMEOUT_US;
	struct bcm_enet_desc *d;
	int ch_idx = iudma->ch_idx;

	if (!iudma->is_tx)
		bcm63xx_fifo_reset_ep(udc, max(0, iudma->ep_num));

	/* stop DMA, then wait for the hardware to wrap up */
	usb_dmac_writel(udc, 0, ENETDMAC_CHANCFG_REG, ch_idx);

	while (usb_dmac_readl(udc, ENETDMAC_CHANCFG_REG, ch_idx) &
				   ENETDMAC_CHANCFG_EN_MASK) {
		udelay(1);

		/* repeatedly flush the FIFO data until the BD completes */
		if (iudma->is_tx && iudma->ep_num >= 0)
			bcm63xx_fifo_reset_ep(udc, iudma->ep_num);

		if (!timeout--) {
			dev_err(udc->dev, "can't reset IUDMA channel %d\n",
				ch_idx);
			break;
		}
		if (timeout == IUDMA_RESET_TIMEOUT_US / 2) {
			dev_warn(udc->dev, "forcibly halting IUDMA channel %d\n",
				 ch_idx);
			usb_dmac_writel(udc, ENETDMAC_CHANCFG_BUFHALT_MASK,
					ENETDMAC_CHANCFG_REG, ch_idx);
		}
	}
	usb_dmac_writel(udc, ~0, ENETDMAC_IR_REG, ch_idx);

	/* don't leave "live" HW-owned entries for the next guy to step on */
	for (d = iudma->bd_ring; d <= iudma->end_bd; d++)
		d->len_stat = 0;
	mb();

	iudma->read_bd = iudma->write_bd = iudma->bd_ring;
	iudma->n_bds_used = 0;

	/* set up IRQs, UBUS burst size, and BD base for this channel */
	usb_dmac_writel(udc, ENETDMAC_IR_BUFDONE_MASK,
			ENETDMAC_IRMASK_REG, ch_idx);
	usb_dmac_writel(udc, 8, ENETDMAC_MAXBURST_REG, ch_idx);

	usb_dmas_writel(udc, iudma->bd_ring_dma, ENETDMAS_RSTART_REG, ch_idx);
	usb_dmas_writel(udc, 0, ENETDMAS_SRAM2_REG, ch_idx);
}

/**
 * iudma_init_channel - One-time IUDMA channel initialization.
 * @udc: Reference to the device controller.
 * @ch_idx: Channel to initialize.
 */
static int iudma_init_channel(struct bcm63xx_udc *udc, unsigned int ch_idx)
{
	struct iudma_ch *iudma = &udc->iudma[ch_idx];
	const struct iudma_ch_cfg *cfg = &iudma_defaults[ch_idx];
	unsigned int n_bds = cfg->n_bds;
	struct bcm63xx_ep *bep = NULL;

	iudma->ep_num = cfg->ep_num;
	iudma->ch_idx = ch_idx;
	iudma->is_tx = !!(ch_idx & 0x01);
	if (iudma->ep_num >= 0) {
		bep = &udc->bep[iudma->ep_num];
		bep->iudma = iudma;
		INIT_LIST_HEAD(&bep->queue);
	}

	iudma->bep = bep;
	iudma->udc = udc;

	/* ep0 is always active; others are controlled by the gadget driver */
	if (iudma->ep_num <= 0)
		iudma->enabled = true;

	iudma->n_bds = n_bds;
	iudma->bd_ring = dmam_alloc_coherent(udc->dev,
		n_bds * sizeof(struct bcm_enet_desc),
		&iudma->bd_ring_dma, GFP_KERNEL);
	if (!iudma->bd_ring)
		return -ENOMEM;
	iudma->end_bd = &iudma->bd_ring[n_bds - 1];

	return 0;
}

/**
 * iudma_init - One-time initialization of all IUDMA channels.
 * @udc: Reference to the device controller.
 *
 * Enable DMA, flush channels, and enable global IUDMA IRQs.
 */
static int iudma_init(struct bcm63xx_udc *udc)
{
	int i, rc;

	usb_dma_writel(udc, ENETDMA_CFG_EN_MASK, ENETDMA_CFG_REG);

	for (i = 0; i < BCM63XX_NUM_IUDMA; i++) {
		rc = iudma_init_channel(udc, i);
		if (rc)
			return rc;
		iudma_reset_channel(udc, &udc->iudma[i]);
	}

	usb_dma_writel(udc, BIT(BCM63XX_NUM_IUDMA)-1, ENETDMA_GLB_IRQMASK_REG);
	return 0;
}

/**
 * iudma_uninit - Uninitialize IUDMA channels.
 * @udc: Reference to the device controller.
 *
 * Kill global IUDMA IRQs, flush channels, and kill DMA.
 */
static void iudma_uninit(struct bcm63xx_udc *udc)
{
	int i;

	usb_dma_writel(udc, 0, ENETDMA_GLB_IRQMASK_REG);

	for (i = 0; i < BCM63XX_NUM_IUDMA; i++)
		iudma_reset_channel(udc, &udc->iudma[i]);

	usb_dma_writel(udc, 0, ENETDMA_CFG_REG);
}

/***********************************************************************
 * Other low-level USBD operations
 ***********************************************************************/

/**
 * bcm63xx_set_ctrl_irqs - Mask/unmask control path interrupts.
 * @udc: Reference to the device controller.
 * @enable_irqs: true to enable, false to disable.
 */
static void bcm63xx_set_ctrl_irqs(struct bcm63xx_udc *udc, bool enable_irqs)
{
	u32 val;

	usbd_writel(udc, 0, USBD_STATUS_REG);

	val = BIT(USBD_EVENT_IRQ_USB_RESET) |
	      BIT(USBD_EVENT_IRQ_SETUP) |
	      BIT(USBD_EVENT_IRQ_SETCFG) |
	      BIT(USBD_EVENT_IRQ_SETINTF) |
	      BIT(USBD_EVENT_IRQ_USB_LINK);
	usbd_writel(udc, enable_irqs ? val : 0, USBD_EVENT_IRQ_MASK_REG);
	usbd_writel(udc, val, USBD_EVENT_IRQ_STATUS_REG);
}

/**
 * bcm63xx_select_phy_mode - Select between USB device and host mode.
 * @udc: Reference to the device controller.
 * @is_device: true for device, false for host.
 *
 * This should probably be reworked to use the drivers/usb/otg
 * infrastructure.
 *
 * By default, the AFE/pullups are disabled in device mode, until
 * bcm63xx_select_pullup() is called.
 */
static void bcm63xx_select_phy_mode(struct bcm63xx_udc *udc, bool is_device)
{
	u32 val, portmask = BIT(udc->pd->port_no);

	if (BCMCPU_IS_6328()) {
		/* configure pinmux to sense VBUS signal */
		val = bcm_gpio_readl(GPIO_PINMUX_OTHR_REG);
		val &= ~GPIO_PINMUX_OTHR_6328_USB_MASK;
		val |= is_device ? GPIO_PINMUX_OTHR_6328_USB_DEV :
			       GPIO_PINMUX_OTHR_6328_USB_HOST;
		bcm_gpio_writel(val, GPIO_PINMUX_OTHR_REG);
	}

	val = bcm_rset_readl(RSET_USBH_PRIV, USBH_PRIV_UTMI_CTL_6368_REG);
	if (is_device) {
		val |= (portmask << USBH_PRIV_UTMI_CTL_HOSTB_SHIFT);
		val |= (portmask << USBH_PRIV_UTMI_CTL_NODRIV_SHIFT);
	} else {
		val &= ~(portmask << USBH_PRIV_UTMI_CTL_HOSTB_SHIFT);
		val &= ~(portmask << USBH_PRIV_UTMI_CTL_NODRIV_SHIFT);
	}
	bcm_rset_writel(RSET_USBH_PRIV, val, USBH_PRIV_UTMI_CTL_6368_REG);

	val = bcm_rset_readl(RSET_USBH_PRIV, USBH_PRIV_SWAP_6368_REG);
	if (is_device)
		val |= USBH_PRIV_SWAP_USBD_MASK;
	else
		val &= ~USBH_PRIV_SWAP_USBD_MASK;
	bcm_rset_writel(RSET_USBH_PRIV, val, USBH_PRIV_SWAP_6368_REG);
}

/**
 * bcm63xx_select_pullup - Enable/disable the pullup on D+
 * @udc: Reference to the device controller.
 * @is_on: true to enable the pullup, false to disable.
 *
 * If the pullup is active, the host will sense a FS/HS device connected to
 * the port.  If the pullup is inactive, the host will think the USB
 * device has been disconnected.
 */
static void bcm63xx_select_pullup(struct bcm63xx_udc *udc, bool is_on)
{
	u32 val, portmask = BIT(udc->pd->port_no);

	val = bcm_rset_readl(RSET_USBH_PRIV, USBH_PRIV_UTMI_CTL_6368_REG);
	if (is_on)
		val &= ~(portmask << USBH_PRIV_UTMI_CTL_NODRIV_SHIFT);
	else
		val |= (portmask << USBH_PRIV_UTMI_CTL_NODRIV_SHIFT);
	bcm_rset_writel(RSET_USBH_PRIV, val, USBH_PRIV_UTMI_CTL_6368_REG);
}

/**
 * bcm63xx_uninit_udc_hw - Shut down the hardware prior to driver removal.
 * @udc: Reference to the device controller.
 *
 * This just masks the IUDMA IRQs and releases the clocks.  It is assumed
 * that bcm63xx_udc_stop() has already run, and the clocks are stopped.
 */
static void bcm63xx_uninit_udc_hw(struct bcm63xx_udc *udc)
{
	set_clocks(udc, true);
	iudma_uninit(udc);
	set_clocks(udc, false);

	clk_put(udc->usbd_clk);
	clk_put(udc->usbh_clk);
}

/**
 * bcm63xx_init_udc_hw - Initialize the controller hardware and data structures.
 * @udc: Reference to the device controller.
 */
static int bcm63xx_init_udc_hw(struct bcm63xx_udc *udc)
{
	int i, rc = 0;
	u32 val;

	udc->ep0_ctrl_buf = devm_kzalloc(udc->dev, BCM63XX_MAX_CTRL_PKT,
					 GFP_KERNEL);
	if (!udc->ep0_ctrl_buf)
		return -ENOMEM;

	INIT_LIST_HEAD(&udc->gadget.ep_list);
	for (i = 0; i < BCM63XX_NUM_EP; i++) {
		struct bcm63xx_ep *bep = &udc->bep[i];

		bep->ep.name = bcm63xx_ep_info[i].name;
		bep->ep.caps = bcm63xx_ep_info[i].caps;
		bep->ep_num = i;
		bep->ep.ops = &bcm63xx_udc_ep_ops;
		list_add_tail(&bep->ep.ep_list, &udc->gadget.ep_list);
		bep->halted = 0;
		usb_ep_set_maxpacket_limit(&bep->ep, BCM63XX_MAX_CTRL_PKT);
		bep->udc = udc;
		bep->ep.desc = NULL;
		INIT_LIST_HEAD(&bep->queue);
	}

	udc->gadget.ep0 = &udc->bep[0].ep;
	list_del(&udc->bep[0].ep.ep_list);

	udc->gadget.speed = USB_SPEED_UNKNOWN;
	udc->ep0state = EP0_SHUTDOWN;

	udc->usbh_clk = clk_get(udc->dev, "usbh");
	if (IS_ERR(udc->usbh_clk))
		return -EIO;

	udc->usbd_clk = clk_get(udc->dev, "usbd");
	if (IS_ERR(udc->usbd_clk)) {
		clk_put(udc->usbh_clk);
		return -EIO;
	}

	set_clocks(udc, true);

	val = USBD_CONTROL_AUTO_CSRS_MASK |
	      USBD_CONTROL_DONE_CSRS_MASK |
	      (irq_coalesce ? USBD_CONTROL_RXZSCFG_MASK : 0);
	usbd_writel(udc, val, USBD_CONTROL_REG);

	val = USBD_STRAPS_APP_SELF_PWR_MASK |
	      USBD_STRAPS_APP_RAM_IF_MASK |
	      USBD_STRAPS_APP_CSRPRGSUP_MASK |
	      USBD_STRAPS_APP_8BITPHY_MASK |
	      USBD_STRAPS_APP_RMTWKUP_MASK;

	if (udc->gadget.max_speed == USB_SPEED_HIGH)
		val |= (BCM63XX_SPD_HIGH << USBD_STRAPS_SPEED_SHIFT);
	else
		val |= (BCM63XX_SPD_FULL << USBD_STRAPS_SPEED_SHIFT);
	usbd_writel(udc, val, USBD_STRAPS_REG);

	bcm63xx_set_ctrl_irqs(udc, false);

	usbd_writel(udc, 0, USBD_EVENT_IRQ_CFG_LO_REG);

	val = USBD_EVENT_IRQ_CFG_FALLING(USBD_EVENT_IRQ_ENUM_ON) |
	      USBD_EVENT_IRQ_CFG_FALLING(USBD_EVENT_IRQ_SET_CSRS);
	usbd_writel(udc, val, USBD_EVENT_IRQ_CFG_HI_REG);

	rc = iudma_init(udc);
	set_clocks(udc, false);
	if (rc)
		bcm63xx_uninit_udc_hw(udc);

	return 0;
}

/***********************************************************************
 * Standard EP gadget operations
 ***********************************************************************/

/**
 * bcm63xx_ep_enable - Enable one endpoint.
 * @ep: Endpoint to enable.
 * @desc: Contains max packet, direction, etc.
 *
 * Most of the endpoint parameters are fixed in this controller, so there
 * isn't much for this function to do.
 */
static int bcm63xx_ep_enable(struct usb_ep *ep,
	const struct usb_endpoint_descriptor *desc)
{
	struct bcm63xx_ep *bep = our_ep(ep);
	struct bcm63xx_udc *udc = bep->udc;
	struct iudma_ch *iudma = bep->iudma;
	unsigned long flags;

	if (!ep || !desc || ep->name == bcm63xx_ep0name)
		return -EINVAL;

	if (!udc->driver)
		return -ESHUTDOWN;

	spin_lock_irqsave(&udc->lock, flags);
	if (iudma->enabled) {
		spin_unlock_irqrestore(&udc->lock, flags);
		return -EINVAL;
	}

	iudma->enabled = true;
	BUG_ON(!list_empty(&bep->queue));

	iudma_reset_channel(udc, iudma);

	bep->halted = 0;
	bcm63xx_set_stall(udc, bep, false);
	clear_bit(bep->ep_num, &udc->wedgemap);

	ep->desc = desc;
	ep->maxpacket = usb_endpoint_maxp(desc);

	spin_unlock_irqrestore(&udc->lock, flags);
	return 0;
}

/**
 * bcm63xx_ep_disable - Disable one endpoint.
 * @ep: Endpoint to disable.
 */
static int bcm63xx_ep_disable(struct usb_ep *ep)
{
	struct bcm63xx_ep *bep = our_ep(ep);
	struct bcm63xx_udc *udc = bep->udc;
	struct iudma_ch *iudma = bep->iudma;
	struct bcm63xx_req *breq, *n;
	unsigned long flags;

	if (!ep || !ep->desc)
		return -EINVAL;

	spin_lock_irqsave(&udc->lock, flags);
	if (!iudma->enabled) {
		spin_unlock_irqrestore(&udc->lock, flags);
		return -EINVAL;
	}
	iudma->enabled = false;

	iudma_reset_channel(udc, iudma);

	if (!list_empty(&bep->queue)) {
		list_for_each_entry_safe(breq, n, &bep->queue, queue) {
			usb_gadget_unmap_request(&udc->gadget, &breq->req,
						 iudma->is_tx);
			list_del(&breq->queue);
			breq->req.status = -ESHUTDOWN;

			spin_unlock_irqrestore(&udc->lock, flags);
			usb_gadget_giveback_request(&iudma->bep->ep, &breq->req);
			spin_lock_irqsave(&udc->lock, flags);
		}
	}
	ep->desc = NULL;

	spin_unlock_irqrestore(&udc->lock, flags);
	return 0;
}

/**
 * bcm63xx_udc_alloc_request - Allocate a new request.
 * @ep: Endpoint associated with the request.
 * @mem_flags: Flags to pass to kzalloc().
 */
static struct usb_request *bcm63xx_udc_alloc_request(struct usb_ep *ep,
	gfp_t mem_flags)
{
	struct bcm63xx_req *breq;

	breq = kzalloc(sizeof(*breq), mem_flags);
	if (!breq)
		return NULL;
	return &breq->req;
}

/**
 * bcm63xx_udc_free_request - Free a request.
 * @ep: Endpoint associated with the request.
 * @req: Request to free.
 */
static void bcm63xx_udc_free_request(struct usb_ep *ep,
	struct usb_request *req)
{
	struct bcm63xx_req *breq = our_req(req);
	kfree(breq);
}

/**
 * bcm63xx_udc_queue - Queue up a new request.
 * @ep: Endpoint associated with the request.
 * @req: Request to add.
 * @mem_flags: Unused.
 *
 * If the queue is empty, start this request immediately.  Otherwise, add
 * it to the list.
 *
 * ep0 replies are sent through this function from the gadget driver, but
 * they are treated differently because they need to be handled by the ep0
 * state machine.  (Sometimes they are replies to control requests that
 * were spoofed by this driver, and so they shouldn't be transmitted at all.)
 */
static int bcm63xx_udc_queue(struct usb_ep *ep, struct usb_request *req,
	gfp_t mem_flags)
{
	struct bcm63xx_ep *bep = our_ep(ep);
	struct bcm63xx_udc *udc = bep->udc;
	struct bcm63xx_req *breq = our_req(req);
	unsigned long flags;
	int rc = 0;

	if (unlikely(!req || !req->complete || !req->buf || !ep))
		return -EINVAL;

	req->actual = 0;
	req->status = 0;
	breq->offset = 0;

	if (bep == &udc->bep[0]) {
		/* only one reply per request, please */
		if (udc->ep0_reply)
			return -EINVAL;

		udc->ep0_reply = req;
		schedule_work(&udc->ep0_wq);
		return 0;
	}

	spin_lock_irqsave(&udc->lock, flags);
	if (!bep->iudma->enabled) {
		rc = -ESHUTDOWN;
		goto out;
	}

	rc = usb_gadget_map_request(&udc->gadget, req, bep->iudma->is_tx);
	if (rc == 0) {
		list_add_tail(&breq->queue, &bep->queue);
		if (list_is_singular(&bep->queue))
			iudma_write(udc, bep->iudma, breq);
	}

out:
	spin_unlock_irqrestore(&udc->lock, flags);
	return rc;
}

/**
 * bcm63xx_udc_dequeue - Remove a pending request from the queue.
 * @ep: Endpoint associated with the request.
 * @req: Request to remove.
 *
 * If the request is not at the head of the queue, this is easy - just nuke
 * it.  If the request is at the head of the queue, we'll need to stop the
 * DMA transaction and then queue up the successor.
 */
static int bcm63xx_udc_dequeue(struct usb_ep *ep, struct usb_request *req)
{
	struct bcm63xx_ep *bep = our_ep(ep);
	struct bcm63xx_udc *udc = bep->udc;
	struct bcm63xx_req *breq = our_req(req), *cur;
	unsigned long flags;
	int rc = 0;

	spin_lock_irqsave(&udc->lock, flags);
	if (list_empty(&bep->queue)) {
		rc = -EINVAL;
		goto out;
	}

	cur = list_first_entry(&bep->queue, struct bcm63xx_req, queue);
	usb_gadget_unmap_request(&udc->gadget, &breq->req, bep->iudma->is_tx);

	if (breq == cur) {
		iudma_reset_channel(udc, bep->iudma);
		list_del(&breq->queue);

		if (!list_empty(&bep->queue)) {
			struct bcm63xx_req *next;

			next = list_first_entry(&bep->queue,
				struct bcm63xx_req, queue);
			iudma_write(udc, bep->iudma, next);
		}
	} else {
		list_del(&breq->queue);
	}

out:
	spin_unlock_irqrestore(&udc->lock, flags);

	req->status = -ESHUTDOWN;
	req->complete(ep, req);

	return rc;
}

/**
 * bcm63xx_udc_set_halt - Enable/disable STALL flag in the hardware.
 * @ep: Endpoint to halt.
 * @value: Zero to clear halt; nonzero to set halt.
 *
 * See comments in bcm63xx_update_wedge().
 */
static int bcm63xx_udc_set_halt(struct usb_ep *ep, int value)
{
	struct bcm63xx_ep *bep = our_ep(ep);
	struct bcm63xx_udc *udc = bep->udc;
	unsigned long flags;

	spin_lock_irqsave(&udc->lock, flags);
	bcm63xx_set_stall(udc, bep, !!value);
	bep->halted = value;
	spin_unlock_irqrestore(&udc->lock, flags);

	return 0;
}

/**
 * bcm63xx_udc_set_wedge - Stall the endpoint until the next reset.
 * @ep: Endpoint to wedge.
 *
 * See comments in bcm63xx_update_wedge().
 */
static int bcm63xx_udc_set_wedge(struct usb_ep *ep)
{
	struct bcm63xx_ep *bep = our_ep(ep);
	struct bcm63xx_udc *udc = bep->udc;
	unsigned long flags;

	spin_lock_irqsave(&udc->lock, flags);
	set_bit(bep->ep_num, &udc->wedgemap);
	bcm63xx_set_stall(udc, bep, true);
	spin_unlock_irqrestore(&udc->lock, flags);

	return 0;
}

static const struct usb_ep_ops bcm63xx_udc_ep_ops = {
	.enable		= bcm63xx_ep_enable,
	.disable	= bcm63xx_ep_disable,

	.alloc_request	= bcm63xx_udc_alloc_request,
	.free_request	= bcm63xx_udc_free_request,

	.queue		= bcm63xx_udc_queue,
	.dequeue	= bcm63xx_udc_dequeue,

	.set_halt	= bcm63xx_udc_set_halt,
	.set_wedge	= bcm63xx_udc_set_wedge,
};

/***********************************************************************
 * EP0 handling
 ***********************************************************************/

/**
 * bcm63xx_ep0_setup_callback - Drop spinlock to invoke ->setup callback.
 * @udc: Reference to the device controller.
 * @ctrl: 8-byte SETUP request.
 */
static int bcm63xx_ep0_setup_callback(struct bcm63xx_udc *udc,
	struct usb_ctrlrequest *ctrl)
{
	int rc;

	spin_unlock_irq(&udc->lock);
	rc = udc->driver->setup(&udc->gadget, ctrl);
	spin_lock_irq(&udc->lock);
	return rc;
}

/**
 * bcm63xx_ep0_spoof_set_cfg - Synthesize a SET_CONFIGURATION request.
 * @udc: Reference to the device controller.
 *
 * Many standard requests are handled automatically in the hardware, but
 * we still need to pass them to the gadget driver so that it can
 * reconfigure the interfaces/endpoints if necessary.
 *
 * Unfortunately we are not able to send a STALL response if the host
 * requests an invalid configuration.  If this happens, we'll have to be
 * content with printing a warning.
 */
static int bcm63xx_ep0_spoof_set_cfg(struct bcm63xx_udc *udc)
{
	struct usb_ctrlrequest ctrl;
	int rc;

	ctrl.bRequestType = USB_DIR_OUT | USB_RECIP_DEVICE;
	ctrl.bRequest = USB_REQ_SET_CONFIGURATION;
	ctrl.wValue = cpu_to_le16(udc->cfg);
	ctrl.wIndex = 0;
	ctrl.wLength = 0;

	rc = bcm63xx_ep0_setup_callback(udc, &ctrl);
	if (rc < 0) {
		dev_warn_ratelimited(udc->dev,
			"hardware auto-acked bad SET_CONFIGURATION(%d) request\n",
			udc->cfg);
	}
	return rc;
}

/**
 * bcm63xx_ep0_spoof_set_iface - Synthesize a SET_INTERFACE request.
 * @udc: Reference to the device controller.
 */
static int bcm63xx_ep0_spoof_set_iface(struct bcm63xx_udc *udc)
{
	struct usb_ctrlrequest ctrl;
	int rc;

	ctrl.bRequestType = USB_DIR_OUT | USB_RECIP_INTERFACE;
	ctrl.bRequest = USB_REQ_SET_INTERFACE;
	ctrl.wValue = cpu_to_le16(udc->alt_iface);
	ctrl.wIndex = cpu_to_le16(udc->iface);
	ctrl.wLength = 0;

	rc = bcm63xx_ep0_setup_callback(udc, &ctrl);
	if (rc < 0) {
		dev_warn_ratelimited(udc->dev,
			"hardware auto-acked bad SET_INTERFACE(%d,%d) request\n",
			udc->iface, udc->alt_iface);
	}
	return rc;
}

/**
 * bcm63xx_ep0_map_write - dma_map and iudma_write a single request.
 * @udc: Reference to the device controller.
 * @ch_idx: IUDMA channel number.
 * @req: USB gadget layer representation of the request.
 */
static void bcm63xx_ep0_map_write(struct bcm63xx_udc *udc, int ch_idx,
	struct usb_request *req)
{
	struct bcm63xx_req *breq = our_req(req);
	struct iudma_ch *iudma = &udc->iudma[ch_idx];

	BUG_ON(udc->ep0_request);
	udc->ep0_request = req;

	req->actual = 0;
	breq->offset = 0;
	usb_gadget_map_request(&udc->gadget, req, iudma->is_tx);
	iudma_write(udc, iudma, breq);
}

/**
 * bcm63xx_ep0_complete - Set completion status and "stage" the callback.
 * @udc: Reference to the device controller.
 * @req: USB gadget layer representation of the request.
 * @status: Status to return to the gadget driver.
 */
static void bcm63xx_ep0_complete(struct bcm63xx_udc *udc,
	struct usb_request *req, int status)
{
	req->status = status;
	if (status)
		req->actual = 0;
	if (req->complete) {
		spin_unlock_irq(&udc->lock);
		req->complete(&udc->bep[0].ep, req);
		spin_lock_irq(&udc->lock);
	}
}

/**
 * bcm63xx_ep0_nuke_reply - Abort request from the gadget driver due to
 *   reset/shutdown.
 * @udc: Reference to the device controller.
 * @is_tx: Nonzero for TX (IN), zero for RX (OUT).
 */
static void bcm63xx_ep0_nuke_reply(struct bcm63xx_udc *udc, int is_tx)
{
	struct usb_request *req = udc->ep0_reply;

	udc->ep0_reply = NULL;
	usb_gadget_unmap_request(&udc->gadget, req, is_tx);
	if (udc->ep0_request == req) {
		udc->ep0_req_completed = 0;
		udc->ep0_request = NULL;
	}
	bcm63xx_ep0_complete(udc, req, -ESHUTDOWN);
}

/**
 * bcm63xx_ep0_read_complete - Close out the pending ep0 request; return
 *   transfer len.
 * @udc: Reference to the device controller.
 */
static int bcm63xx_ep0_read_complete(struct bcm63xx_udc *udc)
{
	struct usb_request *req = udc->ep0_request;

	udc->ep0_req_completed = 0;
	udc->ep0_request = NULL;

	return req->actual;
}

/**
 * bcm63xx_ep0_internal_request - Helper function to submit an ep0 request.
 * @udc: Reference to the device controller.
 * @ch_idx: IUDMA channel number.
 * @length: Number of bytes to TX/RX.
 *
 * Used for simple transfers performed by the ep0 worker.  This will always
 * use ep0_ctrl_req / ep0_ctrl_buf.
 */
static void bcm63xx_ep0_internal_request(struct bcm63xx_udc *udc, int ch_idx,
	int length)
{
	struct usb_request *req = &udc->ep0_ctrl_req.req;

	req->buf = udc->ep0_ctrl_buf;
	req->length = length;
	req->complete = NULL;

	bcm63xx_ep0_map_write(udc, ch_idx, req);
}

/**
 * bcm63xx_ep0_do_setup - Parse new SETUP packet and decide how to handle it.
 * @udc: Reference to the device controller.
 *
 * EP0_IDLE probably shouldn't ever happen.  EP0_REQUEUE means we're ready
 * for the next packet.  Anything else means the transaction requires multiple
 * stages of handling.
 */
static enum bcm63xx_ep0_state bcm63xx_ep0_do_setup(struct bcm63xx_udc *udc)
{
	int rc;
	struct usb_ctrlrequest *ctrl = (void *)udc->ep0_ctrl_buf;

	rc = bcm63xx_ep0_read_complete(udc);

	if (rc < 0) {
		dev_err(udc->dev, "missing SETUP packet\n");
		return EP0_IDLE;
	}

	/*
	 * Handle 0-byte IN STATUS acknowledgement.  The hardware doesn't
	 * ALWAYS deliver these 100% of the time, so if we happen to see one,
	 * just throw it away.
	 */
	if (rc == 0)
		return EP0_REQUEUE;

	/* Drop malformed SETUP packets */
	if (rc != sizeof(*ctrl)) {
		dev_warn_ratelimited(udc->dev,
			"malformed SETUP packet (%d bytes)\n", rc);
		return EP0_REQUEUE;
	}

	/* Process new SETUP packet arriving on ep0 */
	rc = bcm63xx_ep0_setup_callback(udc, ctrl);
	if (rc < 0) {
		bcm63xx_set_stall(udc, &udc->bep[0], true);
		return EP0_REQUEUE;
	}

	if (!ctrl->wLength)
		return EP0_REQUEUE;
	else if (ctrl->bRequestType & USB_DIR_IN)
		return EP0_IN_DATA_PHASE_SETUP;
	else
		return EP0_OUT_DATA_PHASE_SETUP;
}

/**
 * bcm63xx_ep0_do_idle - Check for outstanding requests if ep0 is idle.
 * @udc: Reference to the device controller.
 *
 * In state EP0_IDLE, the RX descriptor is either pending, or has been
 * filled with a SETUP packet from the host.  This function handles new
 * SETUP packets, control IRQ events (which can generate fake SETUP packets),
 * and reset/shutdown events.
 *
 * Returns 0 if work was done; -EAGAIN if nothing to do.
 */
static int bcm63xx_ep0_do_idle(struct bcm63xx_udc *udc)
{
	if (udc->ep0_req_reset) {
		udc->ep0_req_reset = 0;
	} else if (udc->ep0_req_set_cfg) {
		udc->ep0_req_set_cfg = 0;
		if (bcm63xx_ep0_spoof_set_cfg(udc) >= 0)
			udc->ep0state = EP0_IN_FAKE_STATUS_PHASE;
	} else if (udc->ep0_req_set_iface) {
		udc->ep0_req_set_iface = 0;
		if (bcm63xx_ep0_spoof_set_iface(udc) >= 0)
			udc->ep0state = EP0_IN_FAKE_STATUS_PHASE;
	} else if (udc->ep0_req_completed) {
		udc->ep0state = bcm63xx_ep0_do_setup(udc);
		return udc->ep0state == EP0_IDLE ? -EAGAIN : 0;
	} else if (udc->ep0_req_shutdown) {
		udc->ep0_req_shutdown = 0;
		udc->ep0_req_completed = 0;
		udc->ep0_request = NULL;
		iudma_reset_channel(udc, &udc->iudma[IUDMA_EP0_RXCHAN]);
		usb_gadget_unmap_request(&udc->gadget,
			&udc->ep0_ctrl_req.req, 0);

		/* bcm63xx_udc_pullup() is waiting for this */
		mb();
		udc->ep0state = EP0_SHUTDOWN;
	} else if (udc->ep0_reply) {
		/*
		 * This could happen if a USB RESET shows up during an ep0
		 * transaction (especially if a laggy driver like gadgetfs
		 * is in use).
		 */
		dev_warn(udc->dev, "nuking unexpected reply\n");
		bcm63xx_ep0_nuke_reply(udc, 0);
	} else {
		return -EAGAIN;
	}

	return 0;
}

/**
 * bcm63xx_ep0_one_round - Handle the current ep0 state.
 * @udc: Reference to the device controller.
 *
 * Returns 0 if work was done; -EAGAIN if nothing to do.
 */
static int bcm63xx_ep0_one_round(struct bcm63xx_udc *udc)
{
	enum bcm63xx_ep0_state ep0state = udc->ep0state;
	bool shutdown = udc->ep0_req_reset || udc->ep0_req_shutdown;

	switch (udc->ep0state) {
	case EP0_REQUEUE:
		/* set up descriptor to receive SETUP packet */
		bcm63xx_ep0_internal_request(udc, IUDMA_EP0_RXCHAN,
					     BCM63XX_MAX_CTRL_PKT);
		ep0state = EP0_IDLE;
		break;
	case EP0_IDLE:
		return bcm63xx_ep0_do_idle(udc);
	case EP0_IN_DATA_PHASE_SETUP:
		/*
		 * Normal case: TX request is in ep0_reply (queued by the
		 * callback), or will be queued shortly.  When it's here,
		 * send it to the HW and go to EP0_IN_DATA_PHASE_COMPLETE.
		 *
		 * Shutdown case: Stop waiting for the reply.  Just
		 * REQUEUE->IDLE.  The gadget driver is NOT expected to
		 * queue anything else now.
		 */
		if (udc->ep0_reply) {
			bcm63xx_ep0_map_write(udc, IUDMA_EP0_TXCHAN,
					      udc->ep0_reply);
			ep0state = EP0_IN_DATA_PHASE_COMPLETE;
		} else if (shutdown) {
			ep0state = EP0_REQUEUE;
		}
		break;
	case EP0_IN_DATA_PHASE_COMPLETE: {
		/*
		 * Normal case: TX packet (ep0_reply) is in flight; wait for
		 * it to finish, then go back to REQUEUE->IDLE.
		 *
		 * Shutdown case: Reset the TX channel, send -ESHUTDOWN
		 * completion to the gadget driver, then REQUEUE->IDLE.
		 */
		if (udc->ep0_req_completed) {
			udc->ep0_reply = NULL;
			bcm63xx_ep0_read_complete(udc);
			/*
			 * the "ack" sometimes gets eaten (see
			 * bcm63xx_ep0_do_idle)
			 */
			ep0state = EP0_REQUEUE;
		} else if (shutdown) {
			iudma_reset_channel(udc, &udc->iudma[IUDMA_EP0_TXCHAN]);
			bcm63xx_ep0_nuke_reply(udc, 1);
			ep0state = EP0_REQUEUE;
		}
		break;
	}
	case EP0_OUT_DATA_PHASE_SETUP:
		/* Similar behavior to EP0_IN_DATA_PHASE_SETUP */
		if (udc->ep0_reply) {
			bcm63xx_ep0_map_write(udc, IUDMA_EP0_RXCHAN,
					      udc->ep0_reply);
			ep0state = EP0_OUT_DATA_PHASE_COMPLETE;
		} else if (shutdown) {
			ep0state = EP0_REQUEUE;
		}
		break;
	case EP0_OUT_DATA_PHASE_COMPLETE: {
		/* Similar behavior to EP0_IN_DATA_PHASE_COMPLETE */
		if (udc->ep0_req_completed) {
			udc->ep0_reply = NULL;
			bcm63xx_ep0_read_complete(udc);

			/* send 0-byte ack to host */
			bcm63xx_ep0_internal_request(udc, IUDMA_EP0_TXCHAN, 0);
			ep0state = EP0_OUT_STATUS_PHASE;
		} else if (shutdown) {
			iudma_reset_channel(udc, &udc->iudma[IUDMA_EP0_RXCHAN]);
			bcm63xx_ep0_nuke_reply(udc, 0);
			ep0state = EP0_REQUEUE;
		}
		break;
	}
	case EP0_OUT_STATUS_PHASE:
		/*
		 * Normal case: 0-byte OUT ack packet is in flight; wait
		 * for it to finish, then go back to REQUEUE->IDLE.
		 *
		 * Shutdown case: just cancel the transmission.  Don't bother
		 * calling the completion, because it originated from this
		 * function anyway.  Then go back to REQUEUE->IDLE.
		 */
		if (udc->ep0_req_completed) {
			bcm63xx_ep0_read_complete(udc);
			ep0state = EP0_REQUEUE;
		} else if (shutdown) {
			iudma_reset_channel(udc, &udc->iudma[IUDMA_EP0_TXCHAN]);
			udc->ep0_request = NULL;
			ep0state = EP0_REQUEUE;
		}
		break;
	case EP0_IN_FAKE_STATUS_PHASE: {
		/*
		 * Normal case: we spoofed a SETUP packet and are now
		 * waiting for the gadget driver to send a 0-byte reply.
		 * This doesn't actually get sent to the HW because the
		 * HW has already sent its own reply.  Once we get the
		 * response, return to IDLE.
		 *
		 * Shutdown case: return to IDLE immediately.
		 *
		 * Note that the ep0 RX descriptor has remained queued
		 * (and possibly unfilled) during this entire transaction.
		 * The HW datapath (IUDMA) never even sees SET_CONFIGURATION
		 * or SET_INTERFACE transactions.
		 */
		struct usb_request *r = udc->ep0_reply;

		if (!r) {
			if (shutdown)
				ep0state = EP0_IDLE;
			break;
		}

		bcm63xx_ep0_complete(udc, r, 0);
		udc->ep0_reply = NULL;
		ep0state = EP0_IDLE;
		break;
	}
	case EP0_SHUTDOWN:
		break;
	}

	if (udc->ep0state == ep0state)
		return -EAGAIN;

	udc->ep0state = ep0state;
	return 0;
}

/**
 * bcm63xx_ep0_process - ep0 worker thread / state machine.
 * @w: Workqueue struct.
 *
 * bcm63xx_ep0_process is triggered any time an event occurs on ep0.  It
 * is used to synchronize ep0 events and ensure that both HW and SW events
 * occur in a well-defined order.  When the ep0 IUDMA queues are idle, it may
 * synthesize SET_CONFIGURATION / SET_INTERFACE requests that were consumed
 * by the USBD hardware.
 *
 * The worker function will continue iterating around the state machine
 * until there is nothing left to do.  Usually "nothing left to do" means
 * that we're waiting for a new event from the hardware.
 */
static void bcm63xx_ep0_process(struct work_struct *w)
{
	struct bcm63xx_udc *udc = container_of(w, struct bcm63xx_udc, ep0_wq);
	spin_lock_irq(&udc->lock);
	while (bcm63xx_ep0_one_round(udc) == 0)
		;
	spin_unlock_irq(&udc->lock);
}

/***********************************************************************
 * Standard UDC gadget operations
 ***********************************************************************/

/**
 * bcm63xx_udc_get_frame - Read current SOF frame number from the HW.
 * @gadget: USB device.
 */
static int bcm63xx_udc_get_frame(struct usb_gadget *gadget)
{
	struct bcm63xx_udc *udc = gadget_to_udc(gadget);

	return (usbd_readl(udc, USBD_STATUS_REG) &
		USBD_STATUS_SOF_MASK) >> USBD_STATUS_SOF_SHIFT;
}

/**
 * bcm63xx_udc_pullup - Enable/disable pullup on D+ line.
 * @gadget: USB device.
 * @is_on: 0 to disable pullup, 1 to enable.
 *
 * See notes in bcm63xx_select_pullup().
 */
static int bcm63xx_udc_pullup(struct usb_gadget *gadget, int is_on)
{
	struct bcm63xx_udc *udc = gadget_to_udc(gadget);
	unsigned long flags;
	int i, rc = -EINVAL;

	spin_lock_irqsave(&udc->lock, flags);
	if (is_on && udc->ep0state == EP0_SHUTDOWN) {
		udc->gadget.speed = USB_SPEED_UNKNOWN;
		udc->ep0state = EP0_REQUEUE;
		bcm63xx_fifo_setup(udc);
		bcm63xx_fifo_reset(udc);
		bcm63xx_ep_setup(udc);

		bitmap_zero(&udc->wedgemap, BCM63XX_NUM_EP);
		for (i = 0; i < BCM63XX_NUM_EP; i++)
			bcm63xx_set_stall(udc, &udc->bep[i], false);

		bcm63xx_set_ctrl_irqs(udc, true);
		bcm63xx_select_pullup(gadget_to_udc(gadget), true);
		rc = 0;
	} else if (!is_on && udc->ep0state != EP0_SHUTDOWN) {
		bcm63xx_select_pullup(gadget_to_udc(gadget), false);

		udc->ep0_req_shutdown = 1;
		spin_unlock_irqrestore(&udc->lock, flags);

		while (1) {
			schedule_work(&udc->ep0_wq);
			if (udc->ep0state == EP0_SHUTDOWN)
				break;
			msleep(50);
		}
		bcm63xx_set_ctrl_irqs(udc, false);
		cancel_work_sync(&udc->ep0_wq);
		return 0;
	}

	spin_unlock_irqrestore(&udc->lock, flags);
	return rc;
}

/**
 * bcm63xx_udc_start - Start the controller.
 * @gadget: USB device.
 * @driver: Driver for USB device.
 */
static int bcm63xx_udc_start(struct usb_gadget *gadget,
		struct usb_gadget_driver *driver)
{
	struct bcm63xx_udc *udc = gadget_to_udc(gadget);
	unsigned long flags;

	if (!driver || driver->max_speed < USB_SPEED_HIGH ||
	    !driver->setup)
		return -EINVAL;
	if (!udc)
		return -ENODEV;
	if (udc->driver)
		return -EBUSY;

	spin_lock_irqsave(&udc->lock, flags);

	set_clocks(udc, true);
	bcm63xx_fifo_setup(udc);
	bcm63xx_ep_init(udc);
	bcm63xx_ep_setup(udc);
	bcm63xx_fifo_reset(udc);
	bcm63xx_select_phy_mode(udc, true);

	udc->driver = driver;
	udc->gadget.dev.of_node = udc->dev->of_node;

	spin_unlock_irqrestore(&udc->lock, flags);

	return 0;
}

/**
 * bcm63xx_udc_stop - Shut down the controller.
 * @gadget: USB device.
 * @driver: Driver for USB device.
 */
static int bcm63xx_udc_stop(struct usb_gadget *gadget)
{
	struct bcm63xx_udc *udc = gadget_to_udc(gadget);
	unsigned long flags;

	spin_lock_irqsave(&udc->lock, flags);

	udc->driver = NULL;

	/*
	 * If we switch the PHY too abruptly after dropping D+, the host
	 * will often complain:
	 *
	 *     hub 1-0:1.0: port 1 disabled by hub (EMI?), re-enabling...
	 */
	msleep(100);

	bcm63xx_select_phy_mode(udc, false);
	set_clocks(udc, false);

	spin_unlock_irqrestore(&udc->lock, flags);

	return 0;
}

static const struct usb_gadget_ops bcm63xx_udc_ops = {
	.get_frame	= bcm63xx_udc_get_frame,
	.pullup		= bcm63xx_udc_pullup,
	.udc_start	= bcm63xx_udc_start,
	.udc_stop	= bcm63xx_udc_stop,
};

/***********************************************************************
 * IRQ handling
 ***********************************************************************/

/**
 * bcm63xx_update_cfg_iface - Read current configuration/interface settings.
 * @udc: Reference to the device controller.
 *
 * This controller intercepts SET_CONFIGURATION and SET_INTERFACE messages.
 * The driver never sees the raw control packets coming in on the ep0
 * IUDMA channel, but at least we get an interrupt event to tell us that
 * new values are waiting in the USBD_STATUS register.
 */
static void bcm63xx_update_cfg_iface(struct bcm63xx_udc *udc)
{
	u32 reg = usbd_readl(udc, USBD_STATUS_REG);

	udc->cfg = (reg & USBD_STATUS_CFG_MASK) >> USBD_STATUS_CFG_SHIFT;
	udc->iface = (reg & USBD_STATUS_INTF_MASK) >> USBD_STATUS_INTF_SHIFT;
	udc->alt_iface = (reg & USBD_STATUS_ALTINTF_MASK) >>
			 USBD_STATUS_ALTINTF_SHIFT;
	bcm63xx_ep_setup(udc);
}

/**
 * bcm63xx_update_link_speed - Check to see if the link speed has changed.
 * @udc: Reference to the device controller.
 *
 * The link speed update coincides with a SETUP IRQ.  Returns 1 if the
 * speed has changed, so that the caller can update the endpoint settings.
 */
static int bcm63xx_update_link_speed(struct bcm63xx_udc *udc)
{
	u32 reg = usbd_readl(udc, USBD_STATUS_REG);
	enum usb_device_speed oldspeed = udc->gadget.speed;

	switch ((reg & USBD_STATUS_SPD_MASK) >> USBD_STATUS_SPD_SHIFT) {
	case BCM63XX_SPD_HIGH:
		udc->gadget.speed = USB_SPEED_HIGH;
		break;
	case BCM63XX_SPD_FULL:
		udc->gadget.speed = USB_SPEED_FULL;
		break;
	default:
		/* this should never happen */
		udc->gadget.speed = USB_SPEED_UNKNOWN;
		dev_err(udc->dev,
			"received SETUP packet with invalid link speed\n");
		return 0;
	}

	if (udc->gadget.speed != oldspeed) {
		dev_info(udc->dev, "link up, %s-speed mode\n",
			 udc->gadget.speed == USB_SPEED_HIGH ? "high" : "full");
		return 1;
	} else {
		return 0;
	}
}

/**
 * bcm63xx_update_wedge - Iterate through wedged endpoints.
 * @udc: Reference to the device controller.
 * @new_status: true to "refresh" wedge status; false to clear it.
 *
 * On a SETUP interrupt, we need to manually "refresh" the wedge status
 * because the controller hardware is designed to automatically clear
 * stalls in response to a CLEAR_FEATURE request from the host.
 *
 * On a RESET interrupt, we do want to restore all wedged endpoints.
 */
static void bcm63xx_update_wedge(struct bcm63xx_udc *udc, bool new_status)
{
	int i;

	for_each_set_bit(i, &udc->wedgemap, BCM63XX_NUM_EP) {
		bcm63xx_set_stall(udc, &udc->bep[i], new_status);
		if (!new_status)
			clear_bit(i, &udc->wedgemap);
	}
}

/**
 * bcm63xx_udc_ctrl_isr - ISR for control path events (USBD).
 * @irq: IRQ number (unused).
 * @dev_id: Reference to the device controller.
 *
 * This is where we handle link (VBUS) down, USB reset, speed changes,
 * SET_CONFIGURATION, and SET_INTERFACE events.
 */
static irqreturn_t bcm63xx_udc_ctrl_isr(int irq, void *dev_id)
{
	struct bcm63xx_udc *udc = dev_id;
	u32 stat;
	bool disconnected = false, bus_reset = false;

	stat = usbd_readl(udc, USBD_EVENT_IRQ_STATUS_REG) &
	       usbd_readl(udc, USBD_EVENT_IRQ_MASK_REG);

	usbd_writel(udc, stat, USBD_EVENT_IRQ_STATUS_REG);

	spin_lock(&udc->lock);
	if (stat & BIT(USBD_EVENT_IRQ_USB_LINK)) {
		/* VBUS toggled */

		if (!(usbd_readl(udc, USBD_EVENTS_REG) &
		      USBD_EVENTS_USB_LINK_MASK) &&
		      udc->gadget.speed != USB_SPEED_UNKNOWN)
			dev_info(udc->dev, "link down\n");

		udc->gadget.speed = USB_SPEED_UNKNOWN;
		disconnected = true;
	}
	if (stat & BIT(USBD_EVENT_IRQ_USB_RESET)) {
		bcm63xx_fifo_setup(udc);
		bcm63xx_fifo_reset(udc);
		bcm63xx_ep_setup(udc);

		bcm63xx_update_wedge(udc, false);

		udc->ep0_req_reset = 1;
		schedule_work(&udc->ep0_wq);
		bus_reset = true;
	}
	if (stat & BIT(USBD_EVENT_IRQ_SETUP)) {
		if (bcm63xx_update_link_speed(udc)) {
			bcm63xx_fifo_setup(udc);
			bcm63xx_ep_setup(udc);
		}
		bcm63xx_update_wedge(udc, true);
	}
	if (stat & BIT(USBD_EVENT_IRQ_SETCFG)) {
		bcm63xx_update_cfg_iface(udc);
		udc->ep0_req_set_cfg = 1;
		schedule_work(&udc->ep0_wq);
	}
	if (stat & BIT(USBD_EVENT_IRQ_SETINTF)) {
		bcm63xx_update_cfg_iface(udc);
		udc->ep0_req_set_iface = 1;
		schedule_work(&udc->ep0_wq);
	}
	spin_unlock(&udc->lock);

	if (disconnected && udc->driver)
		udc->driver->disconnect(&udc->gadget);
	else if (bus_reset && udc->driver)
		usb_gadget_udc_reset(&udc->gadget, udc->driver);

	return IRQ_HANDLED;
}

/**
 * bcm63xx_udc_data_isr - ISR for data path events (IUDMA).
 * @irq: IRQ number (unused).
 * @dev_id: Reference to the IUDMA channel that generated the interrupt.
 *
 * For the two ep0 channels, we have special handling that triggers the
 * ep0 worker thread.  For normal bulk/intr channels, either queue up
 * the next buffer descriptor for the transaction (incomplete transaction),
 * or invoke the completion callback (complete transactions).
 */
static irqreturn_t bcm63xx_udc_data_isr(int irq, void *dev_id)
{
	struct iudma_ch *iudma = dev_id;
	struct bcm63xx_udc *udc = iudma->udc;
	struct bcm63xx_ep *bep;
	struct usb_request *req = NULL;
	struct bcm63xx_req *breq = NULL;
	int rc;
	bool is_done = false;

	spin_lock(&udc->lock);

	usb_dmac_writel(udc, ENETDMAC_IR_BUFDONE_MASK,
			ENETDMAC_IR_REG, iudma->ch_idx);
	bep = iudma->bep;
	rc = iudma_read(udc, iudma);

	/* special handling for EP0 RX (0) and TX (1) */
	if (iudma->ch_idx == IUDMA_EP0_RXCHAN ||
	    iudma->ch_idx == IUDMA_EP0_TXCHAN) {
		req = udc->ep0_request;
		breq = our_req(req);

		/* a single request could require multiple submissions */
		if (rc >= 0) {
			req->actual += rc;

			if (req->actual >= req->length || breq->bd_bytes > rc) {
				udc->ep0_req_completed = 1;
				is_done = true;
				schedule_work(&udc->ep0_wq);

				/* "actual" on a ZLP is 1 byte */
				req->actual = min(req->actual, req->length);
			} else {
				/* queue up the next BD (same request) */
				iudma_write(udc, iudma, breq);
			}
		}
	} else if (!list_empty(&bep->queue)) {
		breq = list_first_entry(&bep->queue, struct bcm63xx_req, queue);
		req = &breq->req;

		if (rc >= 0) {
			req->actual += rc;

			if (req->actual >= req->length || breq->bd_bytes > rc) {
				is_done = true;
				list_del(&breq->queue);

				req->actual = min(req->actual, req->length);

				if (!list_empty(&bep->queue)) {
					struct bcm63xx_req *next;

					next = list_first_entry(&bep->queue,
						struct bcm63xx_req, queue);
					iudma_write(udc, iudma, next);
				}
			} else {
				iudma_write(udc, iudma, breq);
			}
		}
	}
	spin_unlock(&udc->lock);

	if (is_done) {
		usb_gadget_unmap_request(&udc->gadget, req, iudma->is_tx);
		if (req->complete)
			req->complete(&bep->ep, req);
	}

	return IRQ_HANDLED;
}

/***********************************************************************
 * Debug filesystem
 ***********************************************************************/

/*
 * bcm63xx_usbd_dbg_show - Show USBD controller state.
 * @s: seq_file to which the information will be written.
 * @p: Unused.
 *
 * This file nominally shows up as /sys/kernel/debug/bcm63xx_udc/usbd
 */
static int bcm63xx_usbd_dbg_show(struct seq_file *s, void *p)
{
	struct bcm63xx_udc *udc = s->private;

	if (!udc->driver)
		return -ENODEV;

	seq_printf(s, "ep0 state: %s\n",
		   bcm63xx_ep0_state_names[udc->ep0state]);
	seq_printf(s, "  pending requests: %s%s%s%s%s%s%s\n",
		   udc->ep0_req_reset ? "reset " : "",
		   udc->ep0_req_set_cfg ? "set_cfg " : "",
		   udc->ep0_req_set_iface ? "set_iface " : "",
		   udc->ep0_req_shutdown ? "shutdown " : "",
		   udc->ep0_request ? "pending " : "",
		   udc->ep0_req_completed ? "completed " : "",
		   udc->ep0_reply ? "reply " : "");
	seq_printf(s, "cfg: %d; iface: %d; alt_iface: %d\n",
		   udc->cfg, udc->iface, udc->alt_iface);
	seq_printf(s, "regs:\n");
	seq_printf(s, "  control: %08x; straps: %08x; status: %08x\n",
		   usbd_readl(udc, USBD_CONTROL_REG),
		   usbd_readl(udc, USBD_STRAPS_REG),
		   usbd_readl(udc, USBD_STATUS_REG));
	seq_printf(s, "  events:  %08x; stall:  %08x\n",
		   usbd_readl(udc, USBD_EVENTS_REG),
		   usbd_readl(udc, USBD_STALL_REG));

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(bcm63xx_usbd_dbg);

/*
 * bcm63xx_iudma_dbg_show - Show IUDMA status and descriptors.
 * @s: seq_file to which the information will be written.
 * @p: Unused.
 *
 * This file nominally shows up as /sys/kernel/debug/bcm63xx_udc/iudma
 */
static int bcm63xx_iudma_dbg_show(struct seq_file *s, void *p)
{
	struct bcm63xx_udc *udc = s->private;
	int ch_idx, i;
	u32 sram2, sram3;

	if (!udc->driver)
		return -ENODEV;

	for (ch_idx = 0; ch_idx < BCM63XX_NUM_IUDMA; ch_idx++) {
		struct iudma_ch *iudma = &udc->iudma[ch_idx];

		seq_printf(s, "IUDMA channel %d -- ", ch_idx);
		switch (iudma_defaults[ch_idx].ep_type) {
		case BCMEP_CTRL:
			seq_printf(s, "control");
			break;
		case BCMEP_BULK:
			seq_printf(s, "bulk");
			break;
		case BCMEP_INTR:
			seq_printf(s, "interrupt");
			break;
		}
		seq_printf(s, ch_idx & 0x01 ? " tx" : " rx");
		seq_printf(s, " [ep%d]:\n",
			   max_t(int, iudma_defaults[ch_idx].ep_num, 0));
		seq_printf(s, "  cfg: %08x; irqstat: %08x; irqmask: %08x; maxburst: %08x\n",
			   usb_dmac_readl(udc, ENETDMAC_CHANCFG_REG, ch_idx),
			   usb_dmac_readl(udc, ENETDMAC_IR_REG, ch_idx),
			   usb_dmac_readl(udc, ENETDMAC_IRMASK_REG, ch_idx),
			   usb_dmac_readl(udc, ENETDMAC_MAXBURST_REG, ch_idx));

		sram2 = usb_dmas_readl(udc, ENETDMAS_SRAM2_REG, ch_idx);
		sram3 = usb_dmas_readl(udc, ENETDMAS_SRAM3_REG, ch_idx);
		seq_printf(s, "  base: %08x; index: %04x_%04x; desc: %04x_%04x %08x\n",
			   usb_dmas_readl(udc, ENETDMAS_RSTART_REG, ch_idx),
			   sram2 >> 16, sram2 & 0xffff,
			   sram3 >> 16, sram3 & 0xffff,
			   usb_dmas_readl(udc, ENETDMAS_SRAM4_REG, ch_idx));
		seq_printf(s, "  desc: %d/%d used", iudma->n_bds_used,
			   iudma->n_bds);

		if (iudma->bep)
			seq_printf(s, "; %zu queued\n", list_count_nodes(&iudma->bep->queue));
		else
			seq_printf(s, "\n");

		for (i = 0; i < iudma->n_bds; i++) {
			struct bcm_enet_desc *d = &iudma->bd_ring[i];

			seq_printf(s, "  %03x (%02x): len_stat: %04x_%04x; pa %08x",
				   i * sizeof(*d), i,
				   d->len_stat >> 16, d->len_stat & 0xffff,
				   d->address);
			if (d == iudma->read_bd)
				seq_printf(s, "   <<RD");
			if (d == iudma->write_bd)
				seq_printf(s, "   <<WR");
			seq_printf(s, "\n");
		}

		seq_printf(s, "\n");
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(bcm63xx_iudma_dbg);

/**
 * bcm63xx_udc_init_debugfs - Create debugfs entries.
 * @udc: Reference to the device controller.
 */
static void bcm63xx_udc_init_debugfs(struct bcm63xx_udc *udc)
{
	struct dentry *root;

	if (!IS_ENABLED(CONFIG_USB_GADGET_DEBUG_FS))
		return;

	root = debugfs_create_dir(udc->gadget.name, usb_debug_root);
	debugfs_create_file("usbd", 0400, root, udc, &bcm63xx_usbd_dbg_fops);
	debugfs_create_file("iudma", 0400, root, udc, &bcm63xx_iudma_dbg_fops);
}

/**
 * bcm63xx_udc_cleanup_debugfs - Remove debugfs entries.
 * @udc: Reference to the device controller.
 *
 * debugfs_remove() is safe to call with a NULL argument.
 */
static void bcm63xx_udc_cleanup_debugfs(struct bcm63xx_udc *udc)
{
	debugfs_lookup_and_remove(udc->gadget.name, usb_debug_root);
}

/***********************************************************************
 * Driver init/exit
 ***********************************************************************/

/**
 * bcm63xx_udc_probe - Initialize a new instance of the UDC.
 * @pdev: Platform device struct from the bcm63xx BSP code.
 *
 * Note that platform data is required, because pd.port_no varies from chip
 * to chip and is used to switch the correct USB port to device mode.
 */
static int bcm63xx_udc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct bcm63xx_usbd_platform_data *pd = dev_get_platdata(dev);
	struct bcm63xx_udc *udc;
	int rc = -ENOMEM, i, irq;

	udc = devm_kzalloc(dev, sizeof(*udc), GFP_KERNEL);
	if (!udc)
		return -ENOMEM;

	platform_set_drvdata(pdev, udc);
	udc->dev = dev;
	udc->pd = pd;

	if (!pd) {
		dev_err(dev, "missing platform data\n");
		return -EINVAL;
	}

	udc->usbd_regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(udc->usbd_regs))
		return PTR_ERR(udc->usbd_regs);

	udc->iudma_regs = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(udc->iudma_regs))
		return PTR_ERR(udc->iudma_regs);

	spin_lock_init(&udc->lock);
	INIT_WORK(&udc->ep0_wq, bcm63xx_ep0_process);

	udc->gadget.ops = &bcm63xx_udc_ops;
	udc->gadget.name = dev_name(dev);

	if (!pd->use_fullspeed && !use_fullspeed)
		udc->gadget.max_speed = USB_SPEED_HIGH;
	else
		udc->gadget.max_speed = USB_SPEED_FULL;

	/* request clocks, allocate buffers, and clear any pending IRQs */
	rc = bcm63xx_init_udc_hw(udc);
	if (rc)
		return rc;

	rc = -ENXIO;

	/* IRQ resource #0: control interrupt (VBUS, speed, etc.) */
	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		rc = irq;
		goto out_uninit;
	}
	if (devm_request_irq(dev, irq, &bcm63xx_udc_ctrl_isr, 0,
			     dev_name(dev), udc) < 0)
		goto report_request_failure;

	/* IRQ resources #1-6: data interrupts for IUDMA channels 0-5 */
	for (i = 0; i < BCM63XX_NUM_IUDMA; i++) {
		irq = platform_get_irq(pdev, i + 1);
		if (irq < 0) {
			rc = irq;
			goto out_uninit;
		}
		if (devm_request_irq(dev, irq, &bcm63xx_udc_data_isr, 0,
				     dev_name(dev), &udc->iudma[i]) < 0)
			goto report_request_failure;
	}

	bcm63xx_udc_init_debugfs(udc);
	rc = usb_add_gadget_udc(dev, &udc->gadget);
	if (!rc)
		return 0;

	bcm63xx_udc_cleanup_debugfs(udc);
out_uninit:
	bcm63xx_uninit_udc_hw(udc);
	return rc;

report_request_failure:
	dev_err(dev, "error requesting IRQ #%d\n", irq);
	goto out_uninit;
}

/**
 * bcm63xx_udc_remove - Remove the device from the system.
 * @pdev: Platform device struct from the bcm63xx BSP code.
 */
static void bcm63xx_udc_remove(struct platform_device *pdev)
{
	struct bcm63xx_udc *udc = platform_get_drvdata(pdev);

	bcm63xx_udc_cleanup_debugfs(udc);
	usb_del_gadget_udc(&udc->gadget);
	BUG_ON(udc->driver);

	bcm63xx_uninit_udc_hw(udc);
}

static struct platform_driver bcm63xx_udc_driver = {
	.probe		= bcm63xx_udc_probe,
	.remove_new	= bcm63xx_udc_remove,
	.driver		= {
		.name	= DRV_MODULE_NAME,
	},
};
module_platform_driver(bcm63xx_udc_driver);

MODULE_DESCRIPTION("BCM63xx USB Peripheral Controller");
MODULE_AUTHOR("Kevin Cernekee <cernekee@gmail.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRV_MODULE_NAME);
