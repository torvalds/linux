// SPDX-License-Identifier: GPL-2.0+
/*
 * amd5536.h -- header for AMD 5536 UDC high/full speed USB device controller
 *
 * Copyright (C) 2007 AMD (https://www.amd.com)
 * Author: Thomas Dahlmann
 */

#ifndef AMD5536UDC_H
#define AMD5536UDC_H

/* debug control */
/* #define UDC_VERBOSE */

#include <linux/extcon.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>

/* various constants */
#define UDC_RDE_TIMER_SECONDS		1
#define UDC_RDE_TIMER_DIV		10
#define UDC_POLLSTALL_TIMER_USECONDS	500

/* Hs AMD5536 chip rev. */
#define UDC_HSA0_REV 1
#define UDC_HSB1_REV 2

/* Broadcom chip rev. */
#define UDC_BCM_REV 10

/*
 * SETUP usb commands
 * needed, because some SETUP's are handled in hw, but must be passed to
 * gadget driver above
 * SET_CONFIG
 */
#define UDC_SETCONFIG_DWORD0			0x00000900
#define UDC_SETCONFIG_DWORD0_VALUE_MASK		0xffff0000
#define UDC_SETCONFIG_DWORD0_VALUE_OFS		16

#define UDC_SETCONFIG_DWORD1			0x00000000

/* SET_INTERFACE */
#define UDC_SETINTF_DWORD0			0x00000b00
#define UDC_SETINTF_DWORD0_ALT_MASK		0xffff0000
#define UDC_SETINTF_DWORD0_ALT_OFS		16

#define UDC_SETINTF_DWORD1			0x00000000
#define UDC_SETINTF_DWORD1_INTF_MASK		0x0000ffff
#define UDC_SETINTF_DWORD1_INTF_OFS		0

/* Mass storage reset */
#define UDC_MSCRES_DWORD0			0x0000ff21
#define UDC_MSCRES_DWORD1			0x00000000

/* Global CSR's -------------------------------------------------------------*/
#define UDC_CSR_ADDR				0x500

/* EP NE bits */
/* EP number */
#define UDC_CSR_NE_NUM_MASK			0x0000000f
#define UDC_CSR_NE_NUM_OFS			0
/* EP direction */
#define UDC_CSR_NE_DIR_MASK			0x00000010
#define UDC_CSR_NE_DIR_OFS			4
/* EP type */
#define UDC_CSR_NE_TYPE_MASK			0x00000060
#define UDC_CSR_NE_TYPE_OFS			5
/* EP config number */
#define UDC_CSR_NE_CFG_MASK			0x00000780
#define UDC_CSR_NE_CFG_OFS			7
/* EP interface number */
#define UDC_CSR_NE_INTF_MASK			0x00007800
#define UDC_CSR_NE_INTF_OFS			11
/* EP alt setting */
#define UDC_CSR_NE_ALT_MASK			0x00078000
#define UDC_CSR_NE_ALT_OFS			15

/* max pkt */
#define UDC_CSR_NE_MAX_PKT_MASK			0x3ff80000
#define UDC_CSR_NE_MAX_PKT_OFS			19

/* Device Config Register ---------------------------------------------------*/
#define UDC_DEVCFG_ADDR				0x400

#define UDC_DEVCFG_SOFTRESET			31
#define UDC_DEVCFG_HNPSFEN			30
#define UDC_DEVCFG_DMARST			29
#define UDC_DEVCFG_SET_DESC			18
#define UDC_DEVCFG_CSR_PRG			17
#define UDC_DEVCFG_STATUS			7
#define UDC_DEVCFG_DIR				6
#define UDC_DEVCFG_PI				5
#define UDC_DEVCFG_SS				4
#define UDC_DEVCFG_SP				3
#define UDC_DEVCFG_RWKP				2

#define UDC_DEVCFG_SPD_MASK			0x3
#define UDC_DEVCFG_SPD_OFS			0
#define UDC_DEVCFG_SPD_HS			0x0
#define UDC_DEVCFG_SPD_FS			0x1
#define UDC_DEVCFG_SPD_LS			0x2
/*#define UDC_DEVCFG_SPD_FS			0x3*/


/* Device Control Register --------------------------------------------------*/
#define UDC_DEVCTL_ADDR				0x404

#define UDC_DEVCTL_THLEN_MASK			0xff000000
#define UDC_DEVCTL_THLEN_OFS			24

#define UDC_DEVCTL_BRLEN_MASK			0x00ff0000
#define UDC_DEVCTL_BRLEN_OFS			16

#define UDC_DEVCTL_SRX_FLUSH			14
#define UDC_DEVCTL_CSR_DONE			13
#define UDC_DEVCTL_DEVNAK			12
#define UDC_DEVCTL_SD				10
#define UDC_DEVCTL_MODE				9
#define UDC_DEVCTL_BREN				8
#define UDC_DEVCTL_THE				7
#define UDC_DEVCTL_BF				6
#define UDC_DEVCTL_BE				5
#define UDC_DEVCTL_DU				4
#define UDC_DEVCTL_TDE				3
#define UDC_DEVCTL_RDE				2
#define UDC_DEVCTL_RES				0


/* Device Status Register ---------------------------------------------------*/
#define UDC_DEVSTS_ADDR				0x408

#define UDC_DEVSTS_TS_MASK			0xfffc0000
#define UDC_DEVSTS_TS_OFS			18

#define UDC_DEVSTS_SESSVLD			17
#define UDC_DEVSTS_PHY_ERROR			16
#define UDC_DEVSTS_RXFIFO_EMPTY			15

#define UDC_DEVSTS_ENUM_SPEED_MASK		0x00006000
#define UDC_DEVSTS_ENUM_SPEED_OFS		13
#define UDC_DEVSTS_ENUM_SPEED_FULL		1
#define UDC_DEVSTS_ENUM_SPEED_HIGH		0

#define UDC_DEVSTS_SUSP				12

#define UDC_DEVSTS_ALT_MASK			0x00000f00
#define UDC_DEVSTS_ALT_OFS			8

#define UDC_DEVSTS_INTF_MASK			0x000000f0
#define UDC_DEVSTS_INTF_OFS			4

#define UDC_DEVSTS_CFG_MASK			0x0000000f
#define UDC_DEVSTS_CFG_OFS			0


/* Device Interrupt Register ------------------------------------------------*/
#define UDC_DEVINT_ADDR				0x40c

#define UDC_DEVINT_SVC				7
#define UDC_DEVINT_ENUM				6
#define UDC_DEVINT_SOF				5
#define UDC_DEVINT_US				4
#define UDC_DEVINT_UR				3
#define UDC_DEVINT_ES				2
#define UDC_DEVINT_SI				1
#define UDC_DEVINT_SC				0

/* Device Interrupt Mask Register -------------------------------------------*/
#define UDC_DEVINT_MSK_ADDR			0x410

#define UDC_DEVINT_MSK				0x7f

/* Endpoint Interrupt Register ----------------------------------------------*/
#define UDC_EPINT_ADDR				0x414

#define UDC_EPINT_OUT_MASK			0xffff0000
#define UDC_EPINT_OUT_OFS			16
#define UDC_EPINT_IN_MASK			0x0000ffff
#define UDC_EPINT_IN_OFS			0

#define UDC_EPINT_IN_EP0			0
#define UDC_EPINT_IN_EP1			1
#define UDC_EPINT_IN_EP2			2
#define UDC_EPINT_IN_EP3			3
#define UDC_EPINT_OUT_EP0			16
#define UDC_EPINT_OUT_EP1			17
#define UDC_EPINT_OUT_EP2			18
#define UDC_EPINT_OUT_EP3			19

#define UDC_EPINT_EP0_ENABLE_MSK		0x001e001e

/* Endpoint Interrupt Mask Register -----------------------------------------*/
#define UDC_EPINT_MSK_ADDR			0x418

#define UDC_EPINT_OUT_MSK_MASK			0xffff0000
#define UDC_EPINT_OUT_MSK_OFS			16
#define UDC_EPINT_IN_MSK_MASK			0x0000ffff
#define UDC_EPINT_IN_MSK_OFS			0

#define UDC_EPINT_MSK_DISABLE_ALL		0xffffffff
/* mask non-EP0 endpoints */
#define UDC_EPDATAINT_MSK_DISABLE		0xfffefffe
/* mask all dev interrupts */
#define UDC_DEV_MSK_DISABLE			0x7f

/* Endpoint-specific CSR's --------------------------------------------------*/
#define UDC_EPREGS_ADDR				0x0
#define UDC_EPIN_REGS_ADDR			0x0
#define UDC_EPOUT_REGS_ADDR			0x200

#define UDC_EPCTL_ADDR				0x0

#define UDC_EPCTL_RRDY				9
#define UDC_EPCTL_CNAK				8
#define UDC_EPCTL_SNAK				7
#define UDC_EPCTL_NAK				6

#define UDC_EPCTL_ET_MASK			0x00000030
#define UDC_EPCTL_ET_OFS			4
#define UDC_EPCTL_ET_CONTROL			0
#define UDC_EPCTL_ET_ISO			1
#define UDC_EPCTL_ET_BULK			2
#define UDC_EPCTL_ET_INTERRUPT			3

#define UDC_EPCTL_P				3
#define UDC_EPCTL_SN				2
#define UDC_EPCTL_F				1
#define UDC_EPCTL_S				0

/* Endpoint Status Registers ------------------------------------------------*/
#define UDC_EPSTS_ADDR				0x4

#define UDC_EPSTS_RX_PKT_SIZE_MASK		0x007ff800
#define UDC_EPSTS_RX_PKT_SIZE_OFS		11

#define UDC_EPSTS_TDC				10
#define UDC_EPSTS_HE				9
#define UDC_EPSTS_BNA				7
#define UDC_EPSTS_IN				6

#define UDC_EPSTS_OUT_MASK			0x00000030
#define UDC_EPSTS_OUT_OFS			4
#define UDC_EPSTS_OUT_DATA			1
#define UDC_EPSTS_OUT_DATA_CLEAR		0x10
#define UDC_EPSTS_OUT_SETUP			2
#define UDC_EPSTS_OUT_SETUP_CLEAR		0x20
#define UDC_EPSTS_OUT_CLEAR			0x30

/* Endpoint Buffer Size IN/ Receive Packet Frame Number OUT Registers ------*/
#define UDC_EPIN_BUFF_SIZE_ADDR			0x8
#define UDC_EPOUT_FRAME_NUMBER_ADDR		0x8

#define UDC_EPIN_BUFF_SIZE_MASK			0x0000ffff
#define UDC_EPIN_BUFF_SIZE_OFS			0
/* EP0in txfifo = 128 bytes*/
#define UDC_EPIN0_BUFF_SIZE			32
/* EP0in fullspeed txfifo = 128 bytes*/
#define UDC_FS_EPIN0_BUFF_SIZE			32

/* fifo size mult = fifo size / max packet */
#define UDC_EPIN_BUFF_SIZE_MULT			2

/* EPin data fifo size = 1024 bytes DOUBLE BUFFERING */
#define UDC_EPIN_BUFF_SIZE			256
/* EPin small INT data fifo size = 128 bytes */
#define UDC_EPIN_SMALLINT_BUFF_SIZE		32

/* EPin fullspeed data fifo size = 128 bytes DOUBLE BUFFERING */
#define UDC_FS_EPIN_BUFF_SIZE			32

#define UDC_EPOUT_FRAME_NUMBER_MASK		0x0000ffff
#define UDC_EPOUT_FRAME_NUMBER_OFS		0

/* Endpoint Buffer Size OUT/Max Packet Size Registers -----------------------*/
#define UDC_EPOUT_BUFF_SIZE_ADDR		0x0c
#define UDC_EP_MAX_PKT_SIZE_ADDR		0x0c

#define UDC_EPOUT_BUFF_SIZE_MASK		0xffff0000
#define UDC_EPOUT_BUFF_SIZE_OFS			16
#define UDC_EP_MAX_PKT_SIZE_MASK		0x0000ffff
#define UDC_EP_MAX_PKT_SIZE_OFS			0
/* EP0in max packet size = 64 bytes */
#define UDC_EP0IN_MAX_PKT_SIZE			64
/* EP0out max packet size = 64 bytes */
#define UDC_EP0OUT_MAX_PKT_SIZE			64
/* EP0in fullspeed max packet size = 64 bytes */
#define UDC_FS_EP0IN_MAX_PKT_SIZE		64
/* EP0out fullspeed max packet size = 64 bytes */
#define UDC_FS_EP0OUT_MAX_PKT_SIZE		64

/*
 * Endpoint dma descriptors ------------------------------------------------
 *
 * Setup data, Status dword
 */
#define UDC_DMA_STP_STS_CFG_MASK		0x0fff0000
#define UDC_DMA_STP_STS_CFG_OFS			16
#define UDC_DMA_STP_STS_CFG_ALT_MASK		0x000f0000
#define UDC_DMA_STP_STS_CFG_ALT_OFS		16
#define UDC_DMA_STP_STS_CFG_INTF_MASK		0x00f00000
#define UDC_DMA_STP_STS_CFG_INTF_OFS		20
#define UDC_DMA_STP_STS_CFG_NUM_MASK		0x0f000000
#define UDC_DMA_STP_STS_CFG_NUM_OFS		24
#define UDC_DMA_STP_STS_RX_MASK			0x30000000
#define UDC_DMA_STP_STS_RX_OFS			28
#define UDC_DMA_STP_STS_BS_MASK			0xc0000000
#define UDC_DMA_STP_STS_BS_OFS			30
#define UDC_DMA_STP_STS_BS_HOST_READY		0
#define UDC_DMA_STP_STS_BS_DMA_BUSY		1
#define UDC_DMA_STP_STS_BS_DMA_DONE		2
#define UDC_DMA_STP_STS_BS_HOST_BUSY		3
/* IN data, Status dword */
#define UDC_DMA_IN_STS_TXBYTES_MASK		0x0000ffff
#define UDC_DMA_IN_STS_TXBYTES_OFS		0
#define	UDC_DMA_IN_STS_FRAMENUM_MASK		0x07ff0000
#define UDC_DMA_IN_STS_FRAMENUM_OFS		0
#define UDC_DMA_IN_STS_L			27
#define UDC_DMA_IN_STS_TX_MASK			0x30000000
#define UDC_DMA_IN_STS_TX_OFS			28
#define UDC_DMA_IN_STS_BS_MASK			0xc0000000
#define UDC_DMA_IN_STS_BS_OFS			30
#define UDC_DMA_IN_STS_BS_HOST_READY		0
#define UDC_DMA_IN_STS_BS_DMA_BUSY		1
#define UDC_DMA_IN_STS_BS_DMA_DONE		2
#define UDC_DMA_IN_STS_BS_HOST_BUSY		3
/* OUT data, Status dword */
#define UDC_DMA_OUT_STS_RXBYTES_MASK		0x0000ffff
#define UDC_DMA_OUT_STS_RXBYTES_OFS		0
#define UDC_DMA_OUT_STS_FRAMENUM_MASK		0x07ff0000
#define UDC_DMA_OUT_STS_FRAMENUM_OFS		0
#define UDC_DMA_OUT_STS_L			27
#define UDC_DMA_OUT_STS_RX_MASK			0x30000000
#define UDC_DMA_OUT_STS_RX_OFS			28
#define UDC_DMA_OUT_STS_BS_MASK			0xc0000000
#define UDC_DMA_OUT_STS_BS_OFS			30
#define UDC_DMA_OUT_STS_BS_HOST_READY		0
#define UDC_DMA_OUT_STS_BS_DMA_BUSY		1
#define UDC_DMA_OUT_STS_BS_DMA_DONE		2
#define UDC_DMA_OUT_STS_BS_HOST_BUSY		3
/* max ep0in packet */
#define UDC_EP0IN_MAXPACKET			1000
/* max dma packet */
#define UDC_DMA_MAXPACKET			65536

/* un-usable DMA address */
#define DMA_DONT_USE				(~(dma_addr_t) 0 )

/* other Endpoint register addresses and values-----------------------------*/
#define UDC_EP_SUBPTR_ADDR			0x10
#define UDC_EP_DESPTR_ADDR			0x14
#define UDC_EP_WRITE_CONFIRM_ADDR		0x1c

/* EP number as layouted in AHB space */
#define UDC_EP_NUM				32
#define UDC_EPIN_NUM				16
#define UDC_EPIN_NUM_USED			5
#define UDC_EPOUT_NUM				16
/* EP number of EP's really used = EP0 + 8 data EP's */
#define UDC_USED_EP_NUM				9
/* UDC CSR regs are aligned but AHB regs not - offset for OUT EP's */
#define UDC_CSR_EP_OUT_IX_OFS			12

#define UDC_EP0OUT_IX				16
#define UDC_EP0IN_IX				0

/* Rx fifo address and size = 1k -------------------------------------------*/
#define UDC_RXFIFO_ADDR				0x800
#define UDC_RXFIFO_SIZE				0x400

/* Tx fifo address and size = 1.5k -----------------------------------------*/
#define UDC_TXFIFO_ADDR				0xc00
#define UDC_TXFIFO_SIZE				0x600

/* default data endpoints --------------------------------------------------*/
#define UDC_EPIN_STATUS_IX			1
#define UDC_EPIN_IX				2
#define UDC_EPOUT_IX				18

/* general constants -------------------------------------------------------*/
#define UDC_DWORD_BYTES				4
#define UDC_BITS_PER_BYTE_SHIFT			3
#define UDC_BYTE_MASK				0xff
#define UDC_BITS_PER_BYTE			8

/*---------------------------------------------------------------------------*/
/* UDC CSR's */
struct udc_csrs {

	/* sca - setup command address */
	u32 sca;

	/* ep ne's */
	u32 ne[UDC_USED_EP_NUM];
} __attribute__ ((packed));

/* AHB subsystem CSR registers */
struct udc_regs {

	/* device configuration */
	u32 cfg;

	/* device control */
	u32 ctl;

	/* device status */
	u32 sts;

	/* device interrupt */
	u32 irqsts;

	/* device interrupt mask */
	u32 irqmsk;

	/* endpoint interrupt */
	u32 ep_irqsts;

	/* endpoint interrupt mask */
	u32 ep_irqmsk;
} __attribute__ ((packed));

/* endpoint specific registers */
struct udc_ep_regs {

	/* endpoint control */
	u32 ctl;

	/* endpoint status */
	u32 sts;

	/* endpoint buffer size in/ receive packet frame number out */
	u32 bufin_framenum;

	/* endpoint buffer size out/max packet size */
	u32 bufout_maxpkt;

	/* endpoint setup buffer pointer */
	u32 subptr;

	/* endpoint data descriptor pointer */
	u32 desptr;

	/* reserved */
	u32 reserved;

	/* write/read confirmation */
	u32 confirm;

} __attribute__ ((packed));

/* control data DMA desc */
struct udc_stp_dma {
	/* status quadlet */
	u32	status;
	/* reserved */
	u32	_reserved;
	/* first setup word */
	u32	data12;
	/* second setup word */
	u32	data34;
} __attribute__ ((aligned (16)));

/* normal data DMA desc */
struct udc_data_dma {
	/* status quadlet */
	u32	status;
	/* reserved */
	u32	_reserved;
	/* buffer pointer */
	u32	bufptr;
	/* next descriptor pointer */
	u32	next;
} __attribute__ ((aligned (16)));

/* request packet */
struct udc_request {
	/* embedded gadget ep */
	struct usb_request		req;

	/* flags */
	unsigned			dma_going : 1,
					dma_done : 1;
	/* phys. address */
	dma_addr_t			td_phys;
	/* first dma desc. of chain */
	struct udc_data_dma		*td_data;
	/* last dma desc. of chain */
	struct udc_data_dma		*td_data_last;
	struct list_head		queue;

	/* chain length */
	unsigned			chain_len;

};

/* UDC specific endpoint parameters */
struct udc_ep {
	struct usb_ep			ep;
	struct udc_ep_regs __iomem	*regs;
	u32 __iomem			*txfifo;
	u32 __iomem			*dma;
	dma_addr_t			td_phys;
	dma_addr_t			td_stp_dma;
	struct udc_stp_dma		*td_stp;
	struct udc_data_dma		*td;
	/* temp request */
	struct udc_request		*req;
	unsigned			req_used;
	unsigned			req_completed;
	/* dummy DMA desc for BNA dummy */
	struct udc_request		*bna_dummy_req;
	unsigned			bna_occurred;

	/* NAK state */
	unsigned			naking;

	struct udc			*dev;

	/* queue for requests */
	struct list_head		queue;
	unsigned			halted;
	unsigned			cancel_transfer;
	unsigned			num : 5,
					fifo_depth : 14,
					in : 1;
};

/* device struct */
struct udc {
	struct usb_gadget		gadget;
	spinlock_t			lock;	/* protects all state */
	/* all endpoints */
	struct udc_ep			ep[UDC_EP_NUM];
	struct usb_gadget_driver	*driver;
	/* operational flags */
	unsigned			stall_ep0in : 1,
					waiting_zlp_ack_ep0in : 1,
					set_cfg_not_acked : 1,
					data_ep_enabled : 1,
					data_ep_queued : 1,
					sys_suspended : 1,
					connected;

	u16				chiprev;

	/* registers */
	struct pci_dev			*pdev;
	struct udc_csrs __iomem		*csr;
	struct udc_regs __iomem		*regs;
	struct udc_ep_regs __iomem	*ep_regs;
	u32 __iomem			*rxfifo;
	u32 __iomem			*txfifo;

	/* DMA desc pools */
	struct dma_pool			*data_requests;
	struct dma_pool			*stp_requests;

	/* device data */
	unsigned long			phys_addr;
	void __iomem			*virt_addr;
	unsigned			irq;

	/* states */
	u16				cur_config;
	u16				cur_intf;
	u16				cur_alt;

	/* for platform device and extcon support */
	struct device			*dev;
	struct phy			*udc_phy;
	struct extcon_dev		*edev;
	struct extcon_specific_cable_nb	extcon_nb;
	struct notifier_block		nb;
	struct delayed_work		drd_work;
	u32				conn_type;
};

#define to_amd5536_udc(g)	(container_of((g), struct udc, gadget))

/* setup request data */
union udc_setup_data {
	u32			data[2];
	struct usb_ctrlrequest	request;
};

/* Function declarations */
int udc_enable_dev_setup_interrupts(struct udc *dev);
int udc_mask_unused_interrupts(struct udc *dev);
irqreturn_t udc_irq(int irq, void *pdev);
void gadget_release(struct device *pdev);
void empty_req_queue(struct udc_ep *ep);
void udc_basic_init(struct udc *dev);
void free_dma_pools(struct udc *dev);
int init_dma_pools(struct udc *dev);
void udc_remove(struct udc *dev);
int udc_probe(struct udc *dev);

/* DMA usage flag */
static bool use_dma = 1;
/* packet per buffer dma */
static bool use_dma_ppb = 1;
/* with per descr. update */
static bool use_dma_ppb_du;
/* full speed only mode */
static bool use_fullspeed;

/* module parameters */
module_param(use_dma, bool, S_IRUGO);
MODULE_PARM_DESC(use_dma, "true for DMA");
module_param(use_dma_ppb, bool, S_IRUGO);
MODULE_PARM_DESC(use_dma_ppb, "true for DMA in packet per buffer mode");
module_param(use_dma_ppb_du, bool, S_IRUGO);
MODULE_PARM_DESC(use_dma_ppb_du,
	"true for DMA in packet per buffer mode with descriptor update");
module_param(use_fullspeed, bool, S_IRUGO);
MODULE_PARM_DESC(use_fullspeed, "true for fullspeed only");
/*
 *---------------------------------------------------------------------------
 * SET and GET bitfields in u32 values
 * via constants for mask/offset:
 * <bit_field_stub_name> is the text between
 * UDC_ and _MASK|_OFS of appropriate
 * constant
 *
 * set bitfield value in u32 u32Val
 */
#define AMD_ADDBITS(u32Val, bitfield_val, bitfield_stub_name)		\
	(((u32Val) & (((u32) ~((u32) bitfield_stub_name##_MASK))))	\
	| (((bitfield_val) << ((u32) bitfield_stub_name##_OFS))		\
		& ((u32) bitfield_stub_name##_MASK)))

/*
 * set bitfield value in zero-initialized u32 u32Val
 * => bitfield bits in u32Val are all zero
 */
#define AMD_INIT_SETBITS(u32Val, bitfield_val, bitfield_stub_name)	\
	((u32Val)							\
	| (((bitfield_val) << ((u32) bitfield_stub_name##_OFS))		\
		& ((u32) bitfield_stub_name##_MASK)))

/* get bitfield value from u32 u32Val */
#define AMD_GETBITS(u32Val, bitfield_stub_name)				\
	((u32Val & ((u32) bitfield_stub_name##_MASK))			\
		>> ((u32) bitfield_stub_name##_OFS))

/* SET and GET bits in u32 values ------------------------------------------*/
#define AMD_BIT(bit_stub_name) (1 << bit_stub_name)
#define AMD_UNMASK_BIT(bit_stub_name) (~AMD_BIT(bit_stub_name))
#define AMD_CLEAR_BIT(bit_stub_name) (~AMD_BIT(bit_stub_name))

/* debug macros ------------------------------------------------------------*/

#define DBG(udc , args...)	dev_dbg(udc->dev, args)

#ifdef UDC_VERBOSE
#define VDBG			DBG
#else
#define VDBG(udc , args...)	do {} while (0)
#endif

#endif /* #ifdef AMD5536UDC_H */
