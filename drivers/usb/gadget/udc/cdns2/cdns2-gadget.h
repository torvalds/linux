/* SPDX-License-Identifier: GPL-2.0 */
/*
 * USBHS-DEV device controller driver header file
 *
 * Copyright (C) 2023 Cadence.
 *
 * Author: Pawel Laszczak <pawell@cadence.com>
 */

#ifndef __LINUX_CDNS2_GADGET
#define __LINUX_CDNS2_GADGET

#include <linux/usb/gadget.h>
#include <linux/dma-direction.h>

/*
 * USBHS register interface.
 * This corresponds to the USBHS Device Controller Interface.
 */

/**
 * struct cdns2_ep0_regs - endpoint 0 related registers.
 * @rxbc: receive (OUT) 0 endpoint byte count register.
 * @txbc: transmit (IN) 0 endpoint byte count register.
 * @cs: 0 endpoint control and status register.
 * @reserved1: reserved.
 * @fifo: 0 endpoint fifo register.
 * @reserved2: reserved.
 * @setupdat: SETUP data register.
 * @reserved4: reserved.
 * @maxpack: 0 endpoint max packet size.
 */
struct cdns2_ep0_regs {
	__u8 rxbc;
	__u8 txbc;
	__u8 cs;
	__u8 reserved1[4];
	__u8 fifo;
	__le32 reserved2[94];
	__u8 setupdat[8];
	__u8 reserved4[88];
	__u8 maxpack;
} __packed __aligned(4);

/* EP0CS - bitmasks. */
/* Endpoint 0 stall bit for status stage. */
#define EP0CS_STALL	BIT(0)
/* HSNAK bit. */
#define EP0CS_HSNAK	BIT(1)
/* IN 0 endpoint busy bit. */
#define EP0CS_TXBSY_MSK	BIT(2)
/* OUT 0 endpoint busy bit. */
#define EP0CS_RXBSY_MSK	BIT(3)
/* Send STALL in the data stage phase. */
#define EP0CS_DSTALL	BIT(4)
/* SETUP buffer content was changed. */
#define EP0CS_CHGSET	BIT(7)

/* EP0FIFO - bitmasks. */
/* Direction. */
#define EP0_FIFO_IO_TX	BIT(4)
/* FIFO auto bit. */
#define EP0_FIFO_AUTO	BIT(5)
/* FIFO commit bit. */
#define EP0_FIFO_COMMIT	BIT(6)
/* FIFO access bit. */
#define EP0_FIFO_ACCES	BIT(7)

/**
 * struct cdns2_epx_base - base endpoint registers.
 * @rxbc: OUT endpoint byte count register.
 * @rxcon: OUT endpoint control register.
 * @rxcs: OUT endpoint control and status register.
 * @txbc: IN endpoint byte count register.
 * @txcon: IN endpoint control register.
 * @txcs: IN endpoint control and status register.
 */
struct cdns2_epx_base {
	__le16 rxbc;
	__u8 rxcon;
	__u8 rxcs;
	__le16 txbc;
	__u8 txcon;
	__u8 txcs;
} __packed __aligned(4);

/* rxcon/txcon - endpoint control register bitmasks. */
/* Endpoint buffering: 0 - single buffering ... 3 - quad buffering. */
#define EPX_CON_BUF		GENMASK(1, 0)
/* Endpoint type. */
#define EPX_CON_TYPE		GENMASK(3, 2)
/* Endpoint type: isochronous. */
#define EPX_CON_TYPE_ISOC	0x4
/* Endpoint type: bulk. */
#define EPX_CON_TYPE_BULK	0x8
/* Endpoint type: interrupt. */
#define EPX_CON_TYPE_INT	0xC
/* Number of packets per microframe. */
#define EPX_CON_ISOD		GENMASK(5, 4)
#define EPX_CON_ISOD_SHIFT	0x4
/* Endpoint stall bit. */
#define EPX_CON_STALL		BIT(6)
/* Endpoint enable bit.*/
#define EPX_CON_VAL		BIT(7)

/* rxcs/txcs - endpoint control and status bitmasks. */
/* Data sequence error for the ISO endpoint. */
#define EPX_CS_ERR(p)		((p) & BIT(0))

/**
 * struct cdns2_epx_regs - endpoint 1..15 related registers.
 * @reserved: reserved.
 * @ep: none control endpoints array.
 * @reserved2: reserved.
 * @endprst: endpoint reset register.
 * @reserved3: reserved.
 * @isoautoarm: ISO auto-arm register.
 * @reserved4: reserved.
 * @isodctrl: ISO control register.
 * @reserved5: reserved.
 * @isoautodump: ISO auto dump enable register.
 * @reserved6: reserved.
 * @rxmaxpack: receive (OUT) Max packet size register.
 * @reserved7: reserved.
 * @rxstaddr: receive (OUT) start address endpoint buffer register.
 * @reserved8: reserved.
 * @txstaddr: transmit (IN) start address endpoint buffer register.
 * @reserved9: reserved.
 * @txmaxpack: transmit (IN) Max packet size register.
 */
struct cdns2_epx_regs {
	__le32 reserved[2];
	struct cdns2_epx_base ep[15];
	__u8 reserved2[290];
	__u8 endprst;
	__u8 reserved3[41];
	__le16 isoautoarm;
	__u8 reserved4[10];
	__le16 isodctrl;
	__le16 reserved5;
	__le16 isoautodump;
	__le32 reserved6;
	__le16 rxmaxpack[15];
	__le32 reserved7[65];
	__le32 rxstaddr[15];
	__u8 reserved8[4];
	__le32 txstaddr[15];
	__u8 reserved9[98];
	__le16 txmaxpack[15];
} __packed __aligned(4);

/* ENDPRST - bitmasks. */
/* Endpoint number. */
#define ENDPRST_EP	GENMASK(3, 0)
/* IN direction bit. */
#define ENDPRST_IO_TX	BIT(4)
/* Toggle reset bit. */
#define ENDPRST_TOGRST	BIT(5)
/* FIFO reset bit. */
#define ENDPRST_FIFORST	BIT(6)
/* Toggle status and reset bit. */
#define ENDPRST_TOGSETQ	BIT(7)

/**
 * struct cdns2_interrupt_regs - USB interrupt related registers.
 * @reserved: reserved.
 * @usbirq: USB interrupt request register.
 * @extirq: external interrupt request register.
 * @rxpngirq: external interrupt request register.
 * @reserved1: reserved.
 * @usbien: USB interrupt enable register.
 * @extien: external interrupt enable register.
 * @reserved2: reserved.
 * @usbivect: USB interrupt vector register.
 */
struct cdns2_interrupt_regs {
	__u8 reserved[396];
	__u8 usbirq;
	__u8 extirq;
	__le16 rxpngirq;
	__le16 reserved1[4];
	__u8 usbien;
	__u8 extien;
	__le16 reserved2[3];
	__u8 usbivect;
} __packed __aligned(4);

/* EXTIRQ and EXTIEN - bitmasks. */
/* VBUS fault fall interrupt. */
#define EXTIRQ_VBUSFAULT_FALL BIT(0)
/* VBUS fault fall interrupt. */
#define EXTIRQ_VBUSFAULT_RISE BIT(1)
/* Wake up interrupt bit. */
#define EXTIRQ_WAKEUP	BIT(7)

/* USBIEN and USBIRQ - bitmasks. */
/* SETUP data valid interrupt bit.*/
#define USBIRQ_SUDAV	BIT(0)
/* Start-of-frame interrupt bit. */
#define USBIRQ_SOF	BIT(1)
/* SETUP token interrupt bit. */
#define USBIRQ_SUTOK	BIT(2)
/* USB suspend interrupt bit. */
#define USBIRQ_SUSPEND	BIT(3)
/* USB reset interrupt bit. */
#define USBIRQ_URESET	BIT(4)
/* USB high-speed mode interrupt bit. */
#define USBIRQ_HSPEED	BIT(5)
/* Link Power Management interrupt bit. */
#define USBIRQ_LPM	BIT(7)

#define USB_IEN_INIT (USBIRQ_SUDAV | USBIRQ_SUSPEND | USBIRQ_URESET \
		      | USBIRQ_HSPEED | USBIRQ_LPM)
/**
 * struct cdns2_usb_regs - USB controller registers.
 * @reserved: reserved.
 * @lpmctrl: LPM control register.
 * @lpmclock: LPM clock register.
 * @reserved2: reserved.
 * @endprst: endpoint reset register.
 * @usbcs: USB control and status register.
 * @frmnr: USB frame counter register.
 * @fnaddr: function Address register.
 * @clkgate: clock gate register.
 * @fifoctrl: FIFO control register.
 * @speedctrl: speed Control register.
 * @sleep_clkgate: sleep Clock Gate register.
 * @reserved3: reserved.
 * @cpuctrl: microprocessor control register.
 */
struct cdns2_usb_regs {
	__u8 reserved[4];
	__u16 lpmctrl;
	__u8 lpmclock;
	__u8 reserved2[411];
	__u8 endprst;
	__u8 usbcs;
	__le16 frmnr;
	__u8 fnaddr;
	__u8 clkgate;
	__u8 fifoctrl;
	__u8 speedctrl;
	__u8 sleep_clkgate;
	__u8 reserved3[533];
	__u8 cpuctrl;
} __packed __aligned(4);

/* LPMCTRL - bitmasks. */
/* BESL (Best Effort Service Latency). */
#define LPMCTRLLL_HIRD		GENMASK(7, 4)
/* Last received Remote Wakeup field from LPM Extended Token packet. */
#define LPMCTRLLH_BREMOTEWAKEUP	BIT(8)
/* Reflects value of the lpmnyet bit located in the usbcs[1] register. */
#define LPMCTRLLH_LPMNYET	BIT(16)

/* LPMCLOCK - bitmasks. */
/*
 * If bit is 1 the controller automatically turns off clock
 * (utmisleepm goes to low), else the microprocessor should use
 * sleep clock gate register to turn off clock.
 */
#define LPMCLOCK_SLEEP_ENTRY	BIT(7)

/* USBCS - bitmasks. */
/* Send NYET handshake for the LPM transaction. */
#define USBCS_LPMNYET		BIT(2)
/* Remote wake-up bit. */
#define USBCS_SIGRSUME		BIT(5)
/* Software disconnect bit. */
#define USBCS_DISCON		BIT(6)
/* Indicates that a wakeup pin resumed the controller. */
#define USBCS_WAKESRC		BIT(7)

/* FIFOCTRL - bitmasks. */
/* Endpoint number. */
#define FIFOCTRL_EP		GENMASK(3, 0)
/* Direction bit. */
#define FIFOCTRL_IO_TX		BIT(4)
/* FIFO auto bit. */
#define FIFOCTRL_FIFOAUTO	BIT(5)
/* FIFO commit bit. */
#define FIFOCTRL_FIFOCMIT	BIT(6)
/* FIFO access bit. */
#define FIFOCTRL_FIFOACC	BIT(7)

/* SPEEDCTRL - bitmasks. */
/* Device works in Full Speed. */
#define SPEEDCTRL_FS		BIT(1)
/* Device works in High Speed. */
#define SPEEDCTRL_HS		BIT(2)
/* Force FS mode. */
#define SPEEDCTRL_HSDISABLE	BIT(7)

/* CPUCTRL- bitmasks. */
/* UP clock enable */
#define CPUCTRL_UPCLK		BIT(0)
/* Controller reset bit. */
#define CPUCTRL_SW_RST		BIT(1)
/**
 * If the wuen bit is ‘1’, the upclken is automatically set to ‘1’ after
 * detecting rising edge of wuintereq interrupt. If the wuen bit is ‘0’,
 * the wuintereq interrupt is ignored.
 */
#define CPUCTRL_WUEN		BIT(7)


/**
 * struct cdns2_adma_regs - ADMA controller registers.
 * @conf: DMA global configuration register.
 * @sts: DMA global Status register.
 * @reserved1: reserved.
 * @ep_sel: DMA endpoint select register.
 * @ep_traddr: DMA endpoint transfer ring address register.
 * @ep_cfg: DMA endpoint configuration register.
 * @ep_cmd: DMA endpoint command register.
 * @ep_sts: DMA endpoint status register.
 * @reserved2: reserved.
 * @ep_sts_en: DMA endpoint status enable register.
 * @drbl: DMA doorbell register.
 * @ep_ien: DMA endpoint interrupt enable register.
 * @ep_ists: DMA endpoint interrupt status register.
 * @axim_ctrl: AXI Master Control register.
 * @axim_id: AXI Master ID register.
 * @reserved3: reserved.
 * @axim_cap: AXI Master Wrapper Extended Capability.
 * @reserved4: reserved.
 * @axim_ctrl0: AXI Master Wrapper Extended Capability Control Register 0.
 * @axim_ctrl1: AXI Master Wrapper Extended Capability Control Register 1.
 */
struct cdns2_adma_regs {
	__le32 conf;
	__le32 sts;
	__le32 reserved1[5];
	__le32 ep_sel;
	__le32 ep_traddr;
	__le32 ep_cfg;
	__le32 ep_cmd;
	__le32 ep_sts;
	__le32 reserved2;
	__le32 ep_sts_en;
	__le32 drbl;
	__le32 ep_ien;
	__le32 ep_ists;
	__le32 axim_ctrl;
	__le32 axim_id;
	__le32 reserved3;
	__le32 axim_cap;
	__le32 reserved4;
	__le32 axim_ctrl0;
	__le32 axim_ctrl1;
};

#define CDNS2_ADMA_REGS_OFFSET	0x400

/* DMA_CONF - bitmasks. */
/* Reset USB device configuration. */
#define DMA_CONF_CFGRST		BIT(0)
/* Singular DMA transfer mode.*/
#define DMA_CONF_DSING		BIT(8)
/* Multiple DMA transfers mode.*/
#define DMA_CONF_DMULT		BIT(9)

/* DMA_EP_CFG - bitmasks. */
/* Endpoint enable. */
#define DMA_EP_CFG_ENABLE	BIT(0)

/* DMA_EP_CMD - bitmasks. */
/* Endpoint reset. */
#define DMA_EP_CMD_EPRST	BIT(0)
/* Transfer descriptor ready. */
#define DMA_EP_CMD_DRDY		BIT(6)
/* Data flush. */
#define DMA_EP_CMD_DFLUSH	BIT(7)

/* DMA_EP_STS - bitmasks. */
/* Interrupt On Complete. */
#define DMA_EP_STS_IOC		BIT(2)
/* Interrupt on Short Packet. */
#define DMA_EP_STS_ISP		BIT(3)
/* Transfer descriptor missing. */
#define DMA_EP_STS_DESCMIS	BIT(4)
/* TRB error. */
#define DMA_EP_STS_TRBERR	BIT(7)
/* DMA busy bit. */
#define DMA_EP_STS_DBUSY	BIT(9)
/* Current Cycle Status. */
#define DMA_EP_STS_CCS(p)	((p) & BIT(11))
/* OUT size mismatch. */
#define DMA_EP_STS_OUTSMM	BIT(14)
/* ISO transmission error. */
#define DMA_EP_STS_ISOERR	BIT(15)

/* DMA_EP_STS_EN - bitmasks. */
/* OUT transfer missing descriptor enable. */
#define DMA_EP_STS_EN_DESCMISEN	BIT(4)
/* TRB enable. */
#define DMA_EP_STS_EN_TRBERREN	BIT(7)
/* OUT size mismatch enable. */
#define DMA_EP_STS_EN_OUTSMMEN	BIT(14)
/* ISO transmission error enable. */
#define DMA_EP_STS_EN_ISOERREN	BIT(15)

/* DMA_EP_IEN - bitmasks. */
#define DMA_EP_IEN(index)	(1 << (index))
#define DMA_EP_IEN_EP_OUT0	BIT(0)
#define DMA_EP_IEN_EP_IN0	BIT(16)

/* DMA_EP_ISTS - bitmasks. */
#define DMA_EP_ISTS(index)	(1 << (index))
#define DMA_EP_ISTS_EP_OUT0	BIT(0)
#define DMA_EP_ISTS_EP_IN0	BIT(16)

#define gadget_to_cdns2_device(g) (container_of(g, struct cdns2_device, gadget))
#define ep_to_cdns2_ep(ep) (container_of(ep, struct cdns2_endpoint, endpoint))

/*-------------------------------------------------------------------------*/
#define TRBS_PER_SEGMENT	600
#define ISO_MAX_INTERVAL	8
#define MAX_TRB_LENGTH		BIT(16)
#define MAX_ISO_SIZE		3076
/*
 * To improve performance the TRB buffer pointers can't cross
 * 4KB boundaries.
 */
#define TRB_MAX_ISO_BUFF_SHIFT	12
#define TRB_MAX_ISO_BUFF_SIZE	BIT(TRB_MAX_ISO_BUFF_SHIFT)
/* How much data is left before the 4KB boundary? */
#define TRB_BUFF_LEN_UP_TO_BOUNDARY(addr) (TRB_MAX_ISO_BUFF_SIZE - \
					((addr) & (TRB_MAX_ISO_BUFF_SIZE - 1)))

#if TRBS_PER_SEGMENT < 2
#error "Incorrect TRBS_PER_SEGMENT. Minimal Transfer Ring size is 2."
#endif

/**
 * struct cdns2_trb - represent Transfer Descriptor block.
 * @buffer: pointer to buffer data.
 * @length: length of data.
 * @control: control flags.
 *
 * This structure describes transfer block handled by DMA module.
 */
struct cdns2_trb {
	__le32 buffer;
	__le32 length;
	__le32 control;
};

#define TRB_SIZE		(sizeof(struct cdns2_trb))
/*
 * These two extra TRBs are reserved for isochronous transfer
 * to inject 0 length packet and extra LINK TRB to synchronize the ISO transfer.
 */
#define TRB_ISO_RESERVED	2
#define TR_SEG_SIZE		(TRB_SIZE * (TRBS_PER_SEGMENT + TRB_ISO_RESERVED))

/* TRB bit mask. */
#define TRB_TYPE_BITMASK	GENMASK(15, 10)
#define TRB_TYPE(p)		((p) << 10)
#define TRB_FIELD_TO_TYPE(p)	(((p) & TRB_TYPE_BITMASK) >> 10)

/* TRB type IDs. */
/* Used for Bulk, Interrupt, ISOC, and control data stage. */
#define TRB_NORMAL		1
/* TRB for linking ring segments. */
#define TRB_LINK		6

/* Cycle bit - indicates TRB ownership by driver or hw. */
#define TRB_CYCLE		BIT(0)
/*
 * When set to '1', the device will toggle its interpretation of the Cycle bit.
 */
#define TRB_TOGGLE		BIT(1)
/* Interrupt on short packet. */
#define TRB_ISP			BIT(2)
/* Chain bit associate this TRB with next one TRB. */
#define TRB_CHAIN		BIT(4)
/* Interrupt on completion. */
#define TRB_IOC			BIT(5)

/* Transfer_len bitmasks. */
#define TRB_LEN(p)		((p) & GENMASK(16, 0))
#define TRB_BURST(p)		(((p) << 24) & GENMASK(31, 24))
#define TRB_FIELD_TO_BURST(p)	(((p) & GENMASK(31, 24)) >> 24)

/* Data buffer pointer bitmasks. */
#define TRB_BUFFER(p)		((p) & GENMASK(31, 0))

/*-------------------------------------------------------------------------*/
/* Driver numeric constants. */

/* Maximum address that can be assigned to device. */
#define USB_DEVICE_MAX_ADDRESS	127

/* One control and 15 IN and 15 OUT endpoints. */
#define CDNS2_ENDPOINTS_NUM	31

#define CDNS2_EP_ZLP_BUF_SIZE	512

/*-------------------------------------------------------------------------*/
/* Used structures. */

struct cdns2_device;

/**
 * struct cdns2_ring - transfer ring representation.
 * @trbs: pointer to transfer ring.
 * @dma: dma address of transfer ring.
 * @free_trbs: number of free TRBs in transfer ring.
 * @pcs: producer cycle state.
 * @ccs: consumer cycle state.
 * @enqueue: enqueue index in transfer ring.
 * @dequeue: dequeue index in transfer ring.
 */
struct cdns2_ring {
	struct cdns2_trb *trbs;
	dma_addr_t dma;
	int free_trbs;
	u8 pcs;
	u8 ccs;
	int enqueue;
	int dequeue;
};

/**
 * struct cdns2_endpoint - extended device side representation of USB endpoint.
 * @endpoint: usb endpoint.
 * @pending_list: list of requests queuing on transfer ring.
 * @deferred_list: list of requests waiting for queuing on transfer ring.
 * @pdev: device associated with endpoint.
 * @name: a human readable name e.g. ep1out.
 * @ring: transfer ring associated with endpoint.
 * @ep_state: state of endpoint.
 * @idx: index of endpoint in pdev->eps table.
 * @dir: endpoint direction.
 * @num: endpoint number (1 - 15).
 * @type: set to bmAttributes & USB_ENDPOINT_XFERTYPE_MASK.
 * @interval: interval between packets used for ISOC and Interrupt endpoint.
 * @buffering: on-chip buffers assigned to endpoint.
 * @trb_burst_size: number of burst used in TRB.
 * @skip: Sometimes the controller cannot process isochronous endpoint ring
 *        quickly enough and it will miss some isoc tds on the ring and
 *        generate ISO transmition error.
 *        Driver sets skip flag when receive a ISO transmition error and
 *        process the missed TDs on the endpoint ring.
 * @wa1_set: use WA1.
 * @wa1_trb: TRB assigned to WA1.
 * @wa1_trb_index: TRB index for WA1.
 * @wa1_cycle_bit: correct cycle bit for WA1.
 */
struct cdns2_endpoint {
	struct usb_ep endpoint;
	struct list_head pending_list;
	struct list_head deferred_list;

	struct cdns2_device	*pdev;
	char name[20];

	struct cdns2_ring ring;

#define EP_ENABLED		BIT(0)
#define EP_STALLED		BIT(1)
#define EP_STALL_PENDING	BIT(2)
#define EP_WEDGE		BIT(3)
#define	EP_CLAIMED		BIT(4)
#define EP_RING_FULL		BIT(5)
#define EP_DEFERRED_DRDY	BIT(6)

	u32 ep_state;

	u8 idx;
	u8 dir;
	u8 num;
	u8 type;
	int interval;
	u8 buffering;
	u8 trb_burst_size;
	bool skip;

	unsigned int wa1_set:1;
	struct cdns2_trb *wa1_trb;
	unsigned int wa1_trb_index;
	unsigned int wa1_cycle_bit:1;
};

/**
 * struct cdns2_request - extended device side representation of usb_request
 *                        object.
 * @request: generic usb_request object describing single I/O request.
 * @pep: extended representation of usb_ep object.
 * @trb: the first TRB association with this request.
 * @start_trb: number of the first TRB in transfer ring.
 * @end_trb: number of the last TRB in transfer ring.
 * @list: used for queuing request in lists.
 * @finished_trb: number of trb has already finished per request.
 * @num_of_trb: how many trbs are associated with request.
 */
struct cdns2_request {
	struct usb_request request;
	struct cdns2_endpoint *pep;
	struct cdns2_trb *trb;
	int start_trb;
	int end_trb;
	struct list_head list;
	int finished_trb;
	int num_of_trb;
};

#define to_cdns2_request(r) (container_of(r, struct cdns2_request, request))

/* Stages used during enumeration process.*/
#define CDNS2_SETUP_STAGE		0x0
#define CDNS2_DATA_STAGE		0x1
#define CDNS2_STATUS_STAGE		0x2

/**
 * struct cdns2_device - represent USB device.
 * @dev: pointer to device structure associated whit this controller.
 * @gadget: device side representation of the peripheral controller.
 * @gadget_driver: pointer to the gadget driver.
 * @lock: for synchronizing.
 * @irq: interrupt line number.
 * @regs: base address for registers
 * @usb_regs: base address for common USB registers.
 * @ep0_regs: base address for endpoint 0 related registers.
 * @epx_regs: base address for all none control endpoint registers.
 * @interrupt_regs: base address for interrupt handling related registers.
 * @adma_regs: base address for ADMA registers.
 * @eps_dma_pool: endpoint Transfer Ring pool.
 * @setup: used while processing usb control requests.
 * @ep0_preq: private request used while handling EP0.
 * @ep0_stage: ep0 stage during enumeration process.
 * @zlp_buf: zlp buffer.
 * @dev_address: device address assigned by host.
 * @eps: array of objects describing endpoints.
 * @selected_ep: actually selected endpoint. It's used only to improve
 *      performance by limiting access to dma_ep_sel register.
 * @is_selfpowered: device is self powered.
 * @may_wakeup: allows device to remote wakeup the host.
 * @status_completion_no_call: indicate that driver is waiting for status
 *      stage completion. It's used in deferred SET_CONFIGURATION request.
 * @in_lpm: indicate the controller is in low power mode.
 * @pending_status_wq: workqueue handling status stage for deferred requests.
 * @pending_status_request: request for which status stage was deferred.
 * @eps_supported: endpoints supported by controller in form:
 *      bit: 0 - ep0, 1 - epOut1, 2 - epIn1, 3 - epOut2 ...
 * @burst_opt: array with the best burst size value for different TRB size.
 * @onchip_tx_buf: size of transmit on-chip buffer in KB.
 * @onchip_rx_buf: size of receive on-chip buffer in KB.
 */
struct cdns2_device {
	struct device *dev;
	struct usb_gadget gadget;
	struct usb_gadget_driver *gadget_driver;

	/* generic spin-lock for drivers */
	spinlock_t lock;
	int irq;
	void __iomem *regs;
	struct cdns2_usb_regs __iomem *usb_regs;
	struct cdns2_ep0_regs __iomem *ep0_regs;
	struct cdns2_epx_regs __iomem *epx_regs;
	struct cdns2_interrupt_regs __iomem *interrupt_regs;
	struct cdns2_adma_regs __iomem *adma_regs;
	struct dma_pool *eps_dma_pool;
	struct usb_ctrlrequest setup;
	struct cdns2_request ep0_preq;
	u8 ep0_stage;
	void *zlp_buf;
	u8 dev_address;
	struct cdns2_endpoint eps[CDNS2_ENDPOINTS_NUM];
	u32 selected_ep;
	bool is_selfpowered;
	bool may_wakeup;
	bool status_completion_no_call;
	bool in_lpm;
	struct work_struct pending_status_wq;
	struct usb_request *pending_status_request;
	u32 eps_supported;
	u8 burst_opt[MAX_ISO_SIZE + 1];

	/*in KB */
	u16 onchip_tx_buf;
	u16 onchip_rx_buf;
};

#define CDNS2_IF_EP_EXIST(pdev, ep_num, dir) \
			 ((pdev)->eps_supported & \
			 (BIT(ep_num) << ((dir) ? 0 : 16)))

dma_addr_t cdns2_trb_virt_to_dma(struct cdns2_endpoint *pep,
				 struct cdns2_trb *trb);
void cdns2_pending_setup_status_handler(struct work_struct *work);
void cdns2_select_ep(struct cdns2_device *pdev, u32 ep);
struct cdns2_request *cdns2_next_preq(struct list_head *list);
struct usb_request *cdns2_gadget_ep_alloc_request(struct usb_ep *ep,
						  gfp_t gfp_flags);
void cdns2_gadget_ep_free_request(struct usb_ep *ep,
				  struct usb_request *request);
int cdns2_gadget_ep_dequeue(struct usb_ep *ep, struct usb_request *request);
void cdns2_gadget_giveback(struct cdns2_endpoint *pep,
			   struct cdns2_request *priv_req,
			   int status);
void cdns2_init_ep0(struct cdns2_device *pdev, struct cdns2_endpoint *pep);
void cdns2_ep0_config(struct cdns2_device *pdev);
void cdns2_handle_ep0_interrupt(struct cdns2_device *pdev, int dir);
void cdns2_handle_setup_packet(struct cdns2_device *pdev);
int cdns2_gadget_resume(struct cdns2_device *pdev, bool hibernated);
int cdns2_gadget_suspend(struct cdns2_device *pdev);
void cdns2_gadget_remove(struct cdns2_device *pdev);
int cdns2_gadget_init(struct cdns2_device *pdev);
void set_reg_bit_8(void __iomem *ptr, u8 mask);
int cdns2_halt_endpoint(struct cdns2_device *pdev, struct cdns2_endpoint *pep,
			int value);

#endif /* __LINUX_CDNS2_GADGET */
