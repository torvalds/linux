// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2011 LAPIS Semiconductor Co., Ltd.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/machine.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <linux/irq.h>

#define PCH_VBUS_PERIOD		3000	/* VBUS polling period (msec) */
#define PCH_VBUS_INTERVAL	10	/* VBUS polling interval (msec) */

/* Address offset of Registers */
#define UDC_EP_REG_SHIFT	0x20	/* Offset to next EP */

#define UDC_EPCTL_ADDR		0x00	/* Endpoint control */
#define UDC_EPSTS_ADDR		0x04	/* Endpoint status */
#define UDC_BUFIN_FRAMENUM_ADDR	0x08	/* buffer size in / frame number out */
#define UDC_BUFOUT_MAXPKT_ADDR	0x0C	/* buffer size out / maxpkt in */
#define UDC_SUBPTR_ADDR		0x10	/* setup buffer pointer */
#define UDC_DESPTR_ADDR		0x14	/* Data descriptor pointer */
#define UDC_CONFIRM_ADDR	0x18	/* Write/Read confirmation */

#define UDC_DEVCFG_ADDR		0x400	/* Device configuration */
#define UDC_DEVCTL_ADDR		0x404	/* Device control */
#define UDC_DEVSTS_ADDR		0x408	/* Device status */
#define UDC_DEVIRQSTS_ADDR	0x40C	/* Device irq status */
#define UDC_DEVIRQMSK_ADDR	0x410	/* Device irq mask */
#define UDC_EPIRQSTS_ADDR	0x414	/* Endpoint irq status */
#define UDC_EPIRQMSK_ADDR	0x418	/* Endpoint irq mask */
#define UDC_DEVLPM_ADDR		0x41C	/* LPM control / status */
#define UDC_CSR_BUSY_ADDR	0x4f0	/* UDC_CSR_BUSY Status register */
#define UDC_SRST_ADDR		0x4fc	/* SOFT RESET register */
#define UDC_CSR_ADDR		0x500	/* USB_DEVICE endpoint register */

/* Endpoint control register */
/* Bit position */
#define UDC_EPCTL_MRXFLUSH		(1 << 12)
#define UDC_EPCTL_RRDY			(1 << 9)
#define UDC_EPCTL_CNAK			(1 << 8)
#define UDC_EPCTL_SNAK			(1 << 7)
#define UDC_EPCTL_NAK			(1 << 6)
#define UDC_EPCTL_P			(1 << 3)
#define UDC_EPCTL_F			(1 << 1)
#define UDC_EPCTL_S			(1 << 0)
#define UDC_EPCTL_ET_SHIFT		4
/* Mask patern */
#define UDC_EPCTL_ET_MASK		0x00000030
/* Value for ET field */
#define UDC_EPCTL_ET_CONTROL		0
#define UDC_EPCTL_ET_ISO		1
#define UDC_EPCTL_ET_BULK		2
#define UDC_EPCTL_ET_INTERRUPT		3

/* Endpoint status register */
/* Bit position */
#define UDC_EPSTS_XFERDONE		(1 << 27)
#define UDC_EPSTS_RSS			(1 << 26)
#define UDC_EPSTS_RCS			(1 << 25)
#define UDC_EPSTS_TXEMPTY		(1 << 24)
#define UDC_EPSTS_TDC			(1 << 10)
#define UDC_EPSTS_HE			(1 << 9)
#define UDC_EPSTS_MRXFIFO_EMP		(1 << 8)
#define UDC_EPSTS_BNA			(1 << 7)
#define UDC_EPSTS_IN			(1 << 6)
#define UDC_EPSTS_OUT_SHIFT		4
/* Mask patern */
#define UDC_EPSTS_OUT_MASK		0x00000030
#define UDC_EPSTS_ALL_CLR_MASK		0x1F0006F0
/* Value for OUT field */
#define UDC_EPSTS_OUT_SETUP		2
#define UDC_EPSTS_OUT_DATA		1

/* Device configuration register */
/* Bit position */
#define UDC_DEVCFG_CSR_PRG		(1 << 17)
#define UDC_DEVCFG_SP			(1 << 3)
/* SPD Valee */
#define UDC_DEVCFG_SPD_HS		0x0
#define UDC_DEVCFG_SPD_FS		0x1
#define UDC_DEVCFG_SPD_LS		0x2

/* Device control register */
/* Bit position */
#define UDC_DEVCTL_THLEN_SHIFT		24
#define UDC_DEVCTL_BRLEN_SHIFT		16
#define UDC_DEVCTL_CSR_DONE		(1 << 13)
#define UDC_DEVCTL_SD			(1 << 10)
#define UDC_DEVCTL_MODE			(1 << 9)
#define UDC_DEVCTL_BREN			(1 << 8)
#define UDC_DEVCTL_THE			(1 << 7)
#define UDC_DEVCTL_DU			(1 << 4)
#define UDC_DEVCTL_TDE			(1 << 3)
#define UDC_DEVCTL_RDE			(1 << 2)
#define UDC_DEVCTL_RES			(1 << 0)

/* Device status register */
/* Bit position */
#define UDC_DEVSTS_TS_SHIFT		18
#define UDC_DEVSTS_ENUM_SPEED_SHIFT	13
#define UDC_DEVSTS_ALT_SHIFT		8
#define UDC_DEVSTS_INTF_SHIFT		4
#define UDC_DEVSTS_CFG_SHIFT		0
/* Mask patern */
#define UDC_DEVSTS_TS_MASK		0xfffc0000
#define UDC_DEVSTS_ENUM_SPEED_MASK	0x00006000
#define UDC_DEVSTS_ALT_MASK		0x00000f00
#define UDC_DEVSTS_INTF_MASK		0x000000f0
#define UDC_DEVSTS_CFG_MASK		0x0000000f
/* value for maximum speed for SPEED field */
#define UDC_DEVSTS_ENUM_SPEED_FULL	1
#define UDC_DEVSTS_ENUM_SPEED_HIGH	0
#define UDC_DEVSTS_ENUM_SPEED_LOW	2
#define UDC_DEVSTS_ENUM_SPEED_FULLX	3

/* Device irq register */
/* Bit position */
#define UDC_DEVINT_RWKP			(1 << 7)
#define UDC_DEVINT_ENUM			(1 << 6)
#define UDC_DEVINT_SOF			(1 << 5)
#define UDC_DEVINT_US			(1 << 4)
#define UDC_DEVINT_UR			(1 << 3)
#define UDC_DEVINT_ES			(1 << 2)
#define UDC_DEVINT_SI			(1 << 1)
#define UDC_DEVINT_SC			(1 << 0)
/* Mask patern */
#define UDC_DEVINT_MSK			0x7f

/* Endpoint irq register */
/* Bit position */
#define UDC_EPINT_IN_SHIFT		0
#define UDC_EPINT_OUT_SHIFT		16
#define UDC_EPINT_IN_EP0		(1 << 0)
#define UDC_EPINT_OUT_EP0		(1 << 16)
/* Mask patern */
#define UDC_EPINT_MSK_DISABLE_ALL	0xffffffff

/* UDC_CSR_BUSY Status register */
/* Bit position */
#define UDC_CSR_BUSY			(1 << 0)

/* SOFT RESET register */
/* Bit position */
#define UDC_PSRST			(1 << 1)
#define UDC_SRST			(1 << 0)

/* USB_DEVICE endpoint register */
/* Bit position */
#define UDC_CSR_NE_NUM_SHIFT		0
#define UDC_CSR_NE_DIR_SHIFT		4
#define UDC_CSR_NE_TYPE_SHIFT		5
#define UDC_CSR_NE_CFG_SHIFT		7
#define UDC_CSR_NE_INTF_SHIFT		11
#define UDC_CSR_NE_ALT_SHIFT		15
#define UDC_CSR_NE_MAX_PKT_SHIFT	19
/* Mask patern */
#define UDC_CSR_NE_NUM_MASK		0x0000000f
#define UDC_CSR_NE_DIR_MASK		0x00000010
#define UDC_CSR_NE_TYPE_MASK		0x00000060
#define UDC_CSR_NE_CFG_MASK		0x00000780
#define UDC_CSR_NE_INTF_MASK		0x00007800
#define UDC_CSR_NE_ALT_MASK		0x00078000
#define UDC_CSR_NE_MAX_PKT_MASK		0x3ff80000

#define PCH_UDC_CSR(ep)	(UDC_CSR_ADDR + ep*4)
#define PCH_UDC_EPINT(in, num)\
		(1 << (num + (in ? UDC_EPINT_IN_SHIFT : UDC_EPINT_OUT_SHIFT)))

/* Index of endpoint */
#define UDC_EP0IN_IDX		0
#define UDC_EP0OUT_IDX		1
#define UDC_EPIN_IDX(ep)	(ep * 2)
#define UDC_EPOUT_IDX(ep)	(ep * 2 + 1)
#define PCH_UDC_EP0		0
#define PCH_UDC_EP1		1
#define PCH_UDC_EP2		2
#define PCH_UDC_EP3		3

/* Number of endpoint */
#define PCH_UDC_EP_NUM		32	/* Total number of EPs (16 IN,16 OUT) */
#define PCH_UDC_USED_EP_NUM	4	/* EP number of EP's really used */
/* Length Value */
#define PCH_UDC_BRLEN		0x0F	/* Burst length */
#define PCH_UDC_THLEN		0x1F	/* Threshold length */
/* Value of EP Buffer Size */
#define UDC_EP0IN_BUFF_SIZE	16
#define UDC_EPIN_BUFF_SIZE	256
#define UDC_EP0OUT_BUFF_SIZE	16
#define UDC_EPOUT_BUFF_SIZE	256
/* Value of EP maximum packet size */
#define UDC_EP0IN_MAX_PKT_SIZE	64
#define UDC_EP0OUT_MAX_PKT_SIZE	64
#define UDC_BULK_MAX_PKT_SIZE	512

/* DMA */
#define DMA_DIR_RX		1	/* DMA for data receive */
#define DMA_DIR_TX		2	/* DMA for data transmit */
#define DMA_ADDR_INVALID	(~(dma_addr_t)0)
#define UDC_DMA_MAXPACKET	65536	/* maximum packet size for DMA */

/**
 * struct pch_udc_data_dma_desc - Structure to hold DMA descriptor information
 *				  for data
 * @status:		Status quadlet
 * @reserved:		Reserved
 * @dataptr:		Buffer descriptor
 * @next:		Next descriptor
 */
struct pch_udc_data_dma_desc {
	u32 status;
	u32 reserved;
	u32 dataptr;
	u32 next;
};

/**
 * struct pch_udc_stp_dma_desc - Structure to hold DMA descriptor information
 *				 for control data
 * @status:	Status
 * @reserved:	Reserved
 * @request:	Control Request
 */
struct pch_udc_stp_dma_desc {
	u32 status;
	u32 reserved;
	struct usb_ctrlrequest request;
} __attribute((packed));

/* DMA status definitions */
/* Buffer status */
#define PCH_UDC_BUFF_STS	0xC0000000
#define PCH_UDC_BS_HST_RDY	0x00000000
#define PCH_UDC_BS_DMA_BSY	0x40000000
#define PCH_UDC_BS_DMA_DONE	0x80000000
#define PCH_UDC_BS_HST_BSY	0xC0000000
/*  Rx/Tx Status */
#define PCH_UDC_RXTX_STS	0x30000000
#define PCH_UDC_RTS_SUCC	0x00000000
#define PCH_UDC_RTS_DESERR	0x10000000
#define PCH_UDC_RTS_BUFERR	0x30000000
/* Last Descriptor Indication */
#define PCH_UDC_DMA_LAST	0x08000000
/* Number of Rx/Tx Bytes Mask */
#define PCH_UDC_RXTX_BYTES	0x0000ffff

/**
 * struct pch_udc_cfg_data - Structure to hold current configuration
 *			     and interface information
 * @cur_cfg:	current configuration in use
 * @cur_intf:	current interface in use
 * @cur_alt:	current alt interface in use
 */
struct pch_udc_cfg_data {
	u16 cur_cfg;
	u16 cur_intf;
	u16 cur_alt;
};

/**
 * struct pch_udc_ep - Structure holding a PCH USB device Endpoint information
 * @ep:			embedded ep request
 * @td_stp_phys:	for setup request
 * @td_data_phys:	for data request
 * @td_stp:		for setup request
 * @td_data:		for data request
 * @dev:		reference to device struct
 * @offset_addr:	offset address of ep register
 * @queue:		queue for requests
 * @num:		endpoint number
 * @in:			endpoint is IN
 * @halted:		endpoint halted?
 * @epsts:		Endpoint status
 */
struct pch_udc_ep {
	struct usb_ep			ep;
	dma_addr_t			td_stp_phys;
	dma_addr_t			td_data_phys;
	struct pch_udc_stp_dma_desc	*td_stp;
	struct pch_udc_data_dma_desc	*td_data;
	struct pch_udc_dev		*dev;
	unsigned long			offset_addr;
	struct list_head		queue;
	unsigned			num:5,
					in:1,
					halted:1;
	unsigned long			epsts;
};

/**
 * struct pch_vbus_gpio_data - Structure holding GPIO informaton
 *					for detecting VBUS
 * @port:		gpio descriptor for the VBUS GPIO
 * @intr:		gpio interrupt number
 * @irq_work_fall:	Structure for WorkQueue
 * @irq_work_rise:	Structure for WorkQueue
 */
struct pch_vbus_gpio_data {
	struct gpio_desc	*port;
	int			intr;
	struct work_struct	irq_work_fall;
	struct work_struct	irq_work_rise;
};

/**
 * struct pch_udc_dev - Structure holding complete information
 *			of the PCH USB device
 * @gadget:		gadget driver data
 * @driver:		reference to gadget driver bound
 * @pdev:		reference to the PCI device
 * @ep:			array of endpoints
 * @lock:		protects all state
 * @stall:		stall requested
 * @prot_stall:		protcol stall requested
 * @registered:		driver registered with system
 * @suspended:		driver in suspended state
 * @connected:		gadget driver associated
 * @vbus_session:	required vbus_session state
 * @set_cfg_not_acked:	pending acknowledgement 4 setup
 * @waiting_zlp_ack:	pending acknowledgement 4 ZLP
 * @data_requests:	DMA pool for data requests
 * @stp_requests:	DMA pool for setup requests
 * @dma_addr:		DMA pool for received
 * @setup_data:		Received setup data
 * @base_addr:		for mapped device memory
 * @bar:		PCI BAR used for mapped device memory
 * @cfg_data:		current cfg, intf, and alt in use
 * @vbus_gpio:		GPIO informaton for detecting VBUS
 */
struct pch_udc_dev {
	struct usb_gadget		gadget;
	struct usb_gadget_driver	*driver;
	struct pci_dev			*pdev;
	struct pch_udc_ep		ep[PCH_UDC_EP_NUM];
	spinlock_t			lock; /* protects all state */
	unsigned
			stall:1,
			prot_stall:1,
			suspended:1,
			connected:1,
			vbus_session:1,
			set_cfg_not_acked:1,
			waiting_zlp_ack:1;
	struct dma_pool		*data_requests;
	struct dma_pool		*stp_requests;
	dma_addr_t			dma_addr;
	struct usb_ctrlrequest		setup_data;
	void __iomem			*base_addr;
	unsigned short			bar;
	struct pch_udc_cfg_data		cfg_data;
	struct pch_vbus_gpio_data	vbus_gpio;
};
#define to_pch_udc(g)	(container_of((g), struct pch_udc_dev, gadget))

#define PCH_UDC_PCI_BAR_QUARK_X1000	0
#define PCH_UDC_PCI_BAR			1

#define PCI_DEVICE_ID_INTEL_QUARK_X1000_UDC	0x0939
#define PCI_DEVICE_ID_INTEL_EG20T_UDC		0x8808

#define PCI_DEVICE_ID_ML7213_IOH_UDC	0x801D
#define PCI_DEVICE_ID_ML7831_IOH_UDC	0x8808

static const char	ep0_string[] = "ep0in";
static DEFINE_SPINLOCK(udc_stall_spinlock);	/* stall spin lock */
static bool speed_fs;
module_param_named(speed_fs, speed_fs, bool, S_IRUGO);
MODULE_PARM_DESC(speed_fs, "true for Full speed operation");

/**
 * struct pch_udc_request - Structure holding a PCH USB device request packet
 * @req:		embedded ep request
 * @td_data_phys:	phys. address
 * @td_data:		first dma desc. of chain
 * @td_data_last:	last dma desc. of chain
 * @queue:		associated queue
 * @dma_going:		DMA in progress for request
 * @dma_done:		DMA completed for request
 * @chain_len:		chain length
 */
struct pch_udc_request {
	struct usb_request		req;
	dma_addr_t			td_data_phys;
	struct pch_udc_data_dma_desc	*td_data;
	struct pch_udc_data_dma_desc	*td_data_last;
	struct list_head		queue;
	unsigned			dma_going:1,
					dma_done:1;
	unsigned			chain_len;
};

static inline u32 pch_udc_readl(struct pch_udc_dev *dev, unsigned long reg)
{
	return ioread32(dev->base_addr + reg);
}

static inline void pch_udc_writel(struct pch_udc_dev *dev,
				    unsigned long val, unsigned long reg)
{
	iowrite32(val, dev->base_addr + reg);
}

static inline void pch_udc_bit_set(struct pch_udc_dev *dev,
				     unsigned long reg,
				     unsigned long bitmask)
{
	pch_udc_writel(dev, pch_udc_readl(dev, reg) | bitmask, reg);
}

static inline void pch_udc_bit_clr(struct pch_udc_dev *dev,
				     unsigned long reg,
				     unsigned long bitmask)
{
	pch_udc_writel(dev, pch_udc_readl(dev, reg) & ~(bitmask), reg);
}

static inline u32 pch_udc_ep_readl(struct pch_udc_ep *ep, unsigned long reg)
{
	return ioread32(ep->dev->base_addr + ep->offset_addr + reg);
}

static inline void pch_udc_ep_writel(struct pch_udc_ep *ep,
				    unsigned long val, unsigned long reg)
{
	iowrite32(val, ep->dev->base_addr + ep->offset_addr + reg);
}

static inline void pch_udc_ep_bit_set(struct pch_udc_ep *ep,
				     unsigned long reg,
				     unsigned long bitmask)
{
	pch_udc_ep_writel(ep, pch_udc_ep_readl(ep, reg) | bitmask, reg);
}

static inline void pch_udc_ep_bit_clr(struct pch_udc_ep *ep,
				     unsigned long reg,
				     unsigned long bitmask)
{
	pch_udc_ep_writel(ep, pch_udc_ep_readl(ep, reg) & ~(bitmask), reg);
}

/**
 * pch_udc_csr_busy() - Wait till idle.
 * @dev:	Reference to pch_udc_dev structure
 */
static void pch_udc_csr_busy(struct pch_udc_dev *dev)
{
	unsigned int count = 200;

	/* Wait till idle */
	while ((pch_udc_readl(dev, UDC_CSR_BUSY_ADDR) & UDC_CSR_BUSY)
		&& --count)
		cpu_relax();
	if (!count)
		dev_err(&dev->pdev->dev, "%s: wait error\n", __func__);
}

/**
 * pch_udc_write_csr() - Write the command and status registers.
 * @dev:	Reference to pch_udc_dev structure
 * @val:	value to be written to CSR register
 * @ep:		end-point number
 */
static void pch_udc_write_csr(struct pch_udc_dev *dev, unsigned long val,
			       unsigned int ep)
{
	unsigned long reg = PCH_UDC_CSR(ep);

	pch_udc_csr_busy(dev);		/* Wait till idle */
	pch_udc_writel(dev, val, reg);
	pch_udc_csr_busy(dev);		/* Wait till idle */
}

/**
 * pch_udc_read_csr() - Read the command and status registers.
 * @dev:	Reference to pch_udc_dev structure
 * @ep:		end-point number
 *
 * Return codes:	content of CSR register
 */
static u32 pch_udc_read_csr(struct pch_udc_dev *dev, unsigned int ep)
{
	unsigned long reg = PCH_UDC_CSR(ep);

	pch_udc_csr_busy(dev);		/* Wait till idle */
	pch_udc_readl(dev, reg);	/* Dummy read */
	pch_udc_csr_busy(dev);		/* Wait till idle */
	return pch_udc_readl(dev, reg);
}

/**
 * pch_udc_rmt_wakeup() - Initiate for remote wakeup
 * @dev:	Reference to pch_udc_dev structure
 */
static inline void pch_udc_rmt_wakeup(struct pch_udc_dev *dev)
{
	pch_udc_bit_set(dev, UDC_DEVCTL_ADDR, UDC_DEVCTL_RES);
	mdelay(1);
	pch_udc_bit_clr(dev, UDC_DEVCTL_ADDR, UDC_DEVCTL_RES);
}

/**
 * pch_udc_get_frame() - Get the current frame from device status register
 * @dev:	Reference to pch_udc_dev structure
 * Retern	current frame
 */
static inline int pch_udc_get_frame(struct pch_udc_dev *dev)
{
	u32 frame = pch_udc_readl(dev, UDC_DEVSTS_ADDR);
	return (frame & UDC_DEVSTS_TS_MASK) >> UDC_DEVSTS_TS_SHIFT;
}

/**
 * pch_udc_clear_selfpowered() - Clear the self power control
 * @dev:	Reference to pch_udc_regs structure
 */
static inline void pch_udc_clear_selfpowered(struct pch_udc_dev *dev)
{
	pch_udc_bit_clr(dev, UDC_DEVCFG_ADDR, UDC_DEVCFG_SP);
}

/**
 * pch_udc_set_selfpowered() - Set the self power control
 * @dev:	Reference to pch_udc_regs structure
 */
static inline void pch_udc_set_selfpowered(struct pch_udc_dev *dev)
{
	pch_udc_bit_set(dev, UDC_DEVCFG_ADDR, UDC_DEVCFG_SP);
}

/**
 * pch_udc_set_disconnect() - Set the disconnect status.
 * @dev:	Reference to pch_udc_regs structure
 */
static inline void pch_udc_set_disconnect(struct pch_udc_dev *dev)
{
	pch_udc_bit_set(dev, UDC_DEVCTL_ADDR, UDC_DEVCTL_SD);
}

/**
 * pch_udc_clear_disconnect() - Clear the disconnect status.
 * @dev:	Reference to pch_udc_regs structure
 */
static void pch_udc_clear_disconnect(struct pch_udc_dev *dev)
{
	/* Clear the disconnect */
	pch_udc_bit_set(dev, UDC_DEVCTL_ADDR, UDC_DEVCTL_RES);
	pch_udc_bit_clr(dev, UDC_DEVCTL_ADDR, UDC_DEVCTL_SD);
	mdelay(1);
	/* Resume USB signalling */
	pch_udc_bit_clr(dev, UDC_DEVCTL_ADDR, UDC_DEVCTL_RES);
}

static void pch_udc_init(struct pch_udc_dev *dev);

/**
 * pch_udc_reconnect() - This API initializes usb device controller,
 *						and clear the disconnect status.
 * @dev:		Reference to pch_udc_regs structure
 */
static void pch_udc_reconnect(struct pch_udc_dev *dev)
{
	pch_udc_init(dev);

	/* enable device interrupts */
	/* pch_udc_enable_interrupts() */
	pch_udc_bit_clr(dev, UDC_DEVIRQMSK_ADDR,
			UDC_DEVINT_UR | UDC_DEVINT_ENUM);

	/* Clear the disconnect */
	pch_udc_bit_set(dev, UDC_DEVCTL_ADDR, UDC_DEVCTL_RES);
	pch_udc_bit_clr(dev, UDC_DEVCTL_ADDR, UDC_DEVCTL_SD);
	mdelay(1);
	/* Resume USB signalling */
	pch_udc_bit_clr(dev, UDC_DEVCTL_ADDR, UDC_DEVCTL_RES);
}

/**
 * pch_udc_vbus_session() - set or clearr the disconnect status.
 * @dev:	Reference to pch_udc_regs structure
 * @is_active:	Parameter specifying the action
 *		  0:   indicating VBUS power is ending
 *		  !0:  indicating VBUS power is starting
 */
static inline void pch_udc_vbus_session(struct pch_udc_dev *dev,
					  int is_active)
{
	unsigned long		iflags;

	spin_lock_irqsave(&dev->lock, iflags);
	if (is_active) {
		pch_udc_reconnect(dev);
		dev->vbus_session = 1;
	} else {
		if (dev->driver && dev->driver->disconnect) {
			spin_unlock_irqrestore(&dev->lock, iflags);
			dev->driver->disconnect(&dev->gadget);
			spin_lock_irqsave(&dev->lock, iflags);
		}
		pch_udc_set_disconnect(dev);
		dev->vbus_session = 0;
	}
	spin_unlock_irqrestore(&dev->lock, iflags);
}

/**
 * pch_udc_ep_set_stall() - Set the stall of endpoint
 * @ep:		Reference to structure of type pch_udc_ep_regs
 */
static void pch_udc_ep_set_stall(struct pch_udc_ep *ep)
{
	if (ep->in) {
		pch_udc_ep_bit_set(ep, UDC_EPCTL_ADDR, UDC_EPCTL_F);
		pch_udc_ep_bit_set(ep, UDC_EPCTL_ADDR, UDC_EPCTL_S);
	} else {
		pch_udc_ep_bit_set(ep, UDC_EPCTL_ADDR, UDC_EPCTL_S);
	}
}

/**
 * pch_udc_ep_clear_stall() - Clear the stall of endpoint
 * @ep:		Reference to structure of type pch_udc_ep_regs
 */
static inline void pch_udc_ep_clear_stall(struct pch_udc_ep *ep)
{
	/* Clear the stall */
	pch_udc_ep_bit_clr(ep, UDC_EPCTL_ADDR, UDC_EPCTL_S);
	/* Clear NAK by writing CNAK */
	pch_udc_ep_bit_set(ep, UDC_EPCTL_ADDR, UDC_EPCTL_CNAK);
}

/**
 * pch_udc_ep_set_trfr_type() - Set the transfer type of endpoint
 * @ep:		Reference to structure of type pch_udc_ep_regs
 * @type:	Type of endpoint
 */
static inline void pch_udc_ep_set_trfr_type(struct pch_udc_ep *ep,
					u8 type)
{
	pch_udc_ep_writel(ep, ((type << UDC_EPCTL_ET_SHIFT) &
				UDC_EPCTL_ET_MASK), UDC_EPCTL_ADDR);
}

/**
 * pch_udc_ep_set_bufsz() - Set the maximum packet size for the endpoint
 * @ep:		Reference to structure of type pch_udc_ep_regs
 * @buf_size:	The buffer word size
 * @ep_in:	EP is IN
 */
static void pch_udc_ep_set_bufsz(struct pch_udc_ep *ep,
						 u32 buf_size, u32 ep_in)
{
	u32 data;
	if (ep_in) {
		data = pch_udc_ep_readl(ep, UDC_BUFIN_FRAMENUM_ADDR);
		data = (data & 0xffff0000) | (buf_size & 0xffff);
		pch_udc_ep_writel(ep, data, UDC_BUFIN_FRAMENUM_ADDR);
	} else {
		data = pch_udc_ep_readl(ep, UDC_BUFOUT_MAXPKT_ADDR);
		data = (buf_size << 16) | (data & 0xffff);
		pch_udc_ep_writel(ep, data, UDC_BUFOUT_MAXPKT_ADDR);
	}
}

/**
 * pch_udc_ep_set_maxpkt() - Set the Max packet size for the endpoint
 * @ep:		Reference to structure of type pch_udc_ep_regs
 * @pkt_size:	The packet byte size
 */
static void pch_udc_ep_set_maxpkt(struct pch_udc_ep *ep, u32 pkt_size)
{
	u32 data = pch_udc_ep_readl(ep, UDC_BUFOUT_MAXPKT_ADDR);
	data = (data & 0xffff0000) | (pkt_size & 0xffff);
	pch_udc_ep_writel(ep, data, UDC_BUFOUT_MAXPKT_ADDR);
}

/**
 * pch_udc_ep_set_subptr() - Set the Setup buffer pointer for the endpoint
 * @ep:		Reference to structure of type pch_udc_ep_regs
 * @addr:	Address of the register
 */
static inline void pch_udc_ep_set_subptr(struct pch_udc_ep *ep, u32 addr)
{
	pch_udc_ep_writel(ep, addr, UDC_SUBPTR_ADDR);
}

/**
 * pch_udc_ep_set_ddptr() - Set the Data descriptor pointer for the endpoint
 * @ep:		Reference to structure of type pch_udc_ep_regs
 * @addr:	Address of the register
 */
static inline void pch_udc_ep_set_ddptr(struct pch_udc_ep *ep, u32 addr)
{
	pch_udc_ep_writel(ep, addr, UDC_DESPTR_ADDR);
}

/**
 * pch_udc_ep_set_pd() - Set the poll demand bit for the endpoint
 * @ep:		Reference to structure of type pch_udc_ep_regs
 */
static inline void pch_udc_ep_set_pd(struct pch_udc_ep *ep)
{
	pch_udc_ep_bit_set(ep, UDC_EPCTL_ADDR, UDC_EPCTL_P);
}

/**
 * pch_udc_ep_set_rrdy() - Set the receive ready bit for the endpoint
 * @ep:		Reference to structure of type pch_udc_ep_regs
 */
static inline void pch_udc_ep_set_rrdy(struct pch_udc_ep *ep)
{
	pch_udc_ep_bit_set(ep, UDC_EPCTL_ADDR, UDC_EPCTL_RRDY);
}

/**
 * pch_udc_ep_clear_rrdy() - Clear the receive ready bit for the endpoint
 * @ep:		Reference to structure of type pch_udc_ep_regs
 */
static inline void pch_udc_ep_clear_rrdy(struct pch_udc_ep *ep)
{
	pch_udc_ep_bit_clr(ep, UDC_EPCTL_ADDR, UDC_EPCTL_RRDY);
}

/**
 * pch_udc_set_dma() - Set the 'TDE' or RDE bit of device control
 *			register depending on the direction specified
 * @dev:	Reference to structure of type pch_udc_regs
 * @dir:	whether Tx or Rx
 *		  DMA_DIR_RX: Receive
 *		  DMA_DIR_TX: Transmit
 */
static inline void pch_udc_set_dma(struct pch_udc_dev *dev, int dir)
{
	if (dir == DMA_DIR_RX)
		pch_udc_bit_set(dev, UDC_DEVCTL_ADDR, UDC_DEVCTL_RDE);
	else if (dir == DMA_DIR_TX)
		pch_udc_bit_set(dev, UDC_DEVCTL_ADDR, UDC_DEVCTL_TDE);
}

/**
 * pch_udc_clear_dma() - Clear the 'TDE' or RDE bit of device control
 *				 register depending on the direction specified
 * @dev:	Reference to structure of type pch_udc_regs
 * @dir:	Whether Tx or Rx
 *		  DMA_DIR_RX: Receive
 *		  DMA_DIR_TX: Transmit
 */
static inline void pch_udc_clear_dma(struct pch_udc_dev *dev, int dir)
{
	if (dir == DMA_DIR_RX)
		pch_udc_bit_clr(dev, UDC_DEVCTL_ADDR, UDC_DEVCTL_RDE);
	else if (dir == DMA_DIR_TX)
		pch_udc_bit_clr(dev, UDC_DEVCTL_ADDR, UDC_DEVCTL_TDE);
}

/**
 * pch_udc_set_csr_done() - Set the device control register
 *				CSR done field (bit 13)
 * @dev:	reference to structure of type pch_udc_regs
 */
static inline void pch_udc_set_csr_done(struct pch_udc_dev *dev)
{
	pch_udc_bit_set(dev, UDC_DEVCTL_ADDR, UDC_DEVCTL_CSR_DONE);
}

/**
 * pch_udc_disable_interrupts() - Disables the specified interrupts
 * @dev:	Reference to structure of type pch_udc_regs
 * @mask:	Mask to disable interrupts
 */
static inline void pch_udc_disable_interrupts(struct pch_udc_dev *dev,
					    u32 mask)
{
	pch_udc_bit_set(dev, UDC_DEVIRQMSK_ADDR, mask);
}

/**
 * pch_udc_enable_interrupts() - Enable the specified interrupts
 * @dev:	Reference to structure of type pch_udc_regs
 * @mask:	Mask to enable interrupts
 */
static inline void pch_udc_enable_interrupts(struct pch_udc_dev *dev,
					   u32 mask)
{
	pch_udc_bit_clr(dev, UDC_DEVIRQMSK_ADDR, mask);
}

/**
 * pch_udc_disable_ep_interrupts() - Disable endpoint interrupts
 * @dev:	Reference to structure of type pch_udc_regs
 * @mask:	Mask to disable interrupts
 */
static inline void pch_udc_disable_ep_interrupts(struct pch_udc_dev *dev,
						u32 mask)
{
	pch_udc_bit_set(dev, UDC_EPIRQMSK_ADDR, mask);
}

/**
 * pch_udc_enable_ep_interrupts() - Enable endpoint interrupts
 * @dev:	Reference to structure of type pch_udc_regs
 * @mask:	Mask to enable interrupts
 */
static inline void pch_udc_enable_ep_interrupts(struct pch_udc_dev *dev,
					      u32 mask)
{
	pch_udc_bit_clr(dev, UDC_EPIRQMSK_ADDR, mask);
}

/**
 * pch_udc_read_device_interrupts() - Read the device interrupts
 * @dev:	Reference to structure of type pch_udc_regs
 * Retern	The device interrupts
 */
static inline u32 pch_udc_read_device_interrupts(struct pch_udc_dev *dev)
{
	return pch_udc_readl(dev, UDC_DEVIRQSTS_ADDR);
}

/**
 * pch_udc_write_device_interrupts() - Write device interrupts
 * @dev:	Reference to structure of type pch_udc_regs
 * @val:	The value to be written to interrupt register
 */
static inline void pch_udc_write_device_interrupts(struct pch_udc_dev *dev,
						     u32 val)
{
	pch_udc_writel(dev, val, UDC_DEVIRQSTS_ADDR);
}

/**
 * pch_udc_read_ep_interrupts() - Read the endpoint interrupts
 * @dev:	Reference to structure of type pch_udc_regs
 * Retern	The endpoint interrupt
 */
static inline u32 pch_udc_read_ep_interrupts(struct pch_udc_dev *dev)
{
	return pch_udc_readl(dev, UDC_EPIRQSTS_ADDR);
}

/**
 * pch_udc_write_ep_interrupts() - Clear endpoint interupts
 * @dev:	Reference to structure of type pch_udc_regs
 * @val:	The value to be written to interrupt register
 */
static inline void pch_udc_write_ep_interrupts(struct pch_udc_dev *dev,
					     u32 val)
{
	pch_udc_writel(dev, val, UDC_EPIRQSTS_ADDR);
}

/**
 * pch_udc_read_device_status() - Read the device status
 * @dev:	Reference to structure of type pch_udc_regs
 * Retern	The device status
 */
static inline u32 pch_udc_read_device_status(struct pch_udc_dev *dev)
{
	return pch_udc_readl(dev, UDC_DEVSTS_ADDR);
}

/**
 * pch_udc_read_ep_control() - Read the endpoint control
 * @ep:		Reference to structure of type pch_udc_ep_regs
 * Retern	The endpoint control register value
 */
static inline u32 pch_udc_read_ep_control(struct pch_udc_ep *ep)
{
	return pch_udc_ep_readl(ep, UDC_EPCTL_ADDR);
}

/**
 * pch_udc_clear_ep_control() - Clear the endpoint control register
 * @ep:		Reference to structure of type pch_udc_ep_regs
 * Retern	The endpoint control register value
 */
static inline void pch_udc_clear_ep_control(struct pch_udc_ep *ep)
{
	return pch_udc_ep_writel(ep, 0, UDC_EPCTL_ADDR);
}

/**
 * pch_udc_read_ep_status() - Read the endpoint status
 * @ep:		Reference to structure of type pch_udc_ep_regs
 * Retern	The endpoint status
 */
static inline u32 pch_udc_read_ep_status(struct pch_udc_ep *ep)
{
	return pch_udc_ep_readl(ep, UDC_EPSTS_ADDR);
}

/**
 * pch_udc_clear_ep_status() - Clear the endpoint status
 * @ep:		Reference to structure of type pch_udc_ep_regs
 * @stat:	Endpoint status
 */
static inline void pch_udc_clear_ep_status(struct pch_udc_ep *ep,
					 u32 stat)
{
	return pch_udc_ep_writel(ep, stat, UDC_EPSTS_ADDR);
}

/**
 * pch_udc_ep_set_nak() - Set the bit 7 (SNAK field)
 *				of the endpoint control register
 * @ep:		Reference to structure of type pch_udc_ep_regs
 */
static inline void pch_udc_ep_set_nak(struct pch_udc_ep *ep)
{
	pch_udc_ep_bit_set(ep, UDC_EPCTL_ADDR, UDC_EPCTL_SNAK);
}

/**
 * pch_udc_ep_clear_nak() - Set the bit 8 (CNAK field)
 *				of the endpoint control register
 * @ep:		reference to structure of type pch_udc_ep_regs
 */
static void pch_udc_ep_clear_nak(struct pch_udc_ep *ep)
{
	unsigned int loopcnt = 0;
	struct pch_udc_dev *dev = ep->dev;

	if (!(pch_udc_ep_readl(ep, UDC_EPCTL_ADDR) & UDC_EPCTL_NAK))
		return;
	if (!ep->in) {
		loopcnt = 10000;
		while (!(pch_udc_read_ep_status(ep) & UDC_EPSTS_MRXFIFO_EMP) &&
			--loopcnt)
			udelay(5);
		if (!loopcnt)
			dev_err(&dev->pdev->dev, "%s: RxFIFO not Empty\n",
				__func__);
	}
	loopcnt = 10000;
	while ((pch_udc_read_ep_control(ep) & UDC_EPCTL_NAK) && --loopcnt) {
		pch_udc_ep_bit_set(ep, UDC_EPCTL_ADDR, UDC_EPCTL_CNAK);
		udelay(5);
	}
	if (!loopcnt)
		dev_err(&dev->pdev->dev, "%s: Clear NAK not set for ep%d%s\n",
			__func__, ep->num, (ep->in ? "in" : "out"));
}

/**
 * pch_udc_ep_fifo_flush() - Flush the endpoint fifo
 * @ep:	reference to structure of type pch_udc_ep_regs
 * @dir:	direction of endpoint
 *		  0:  endpoint is OUT
 *		  !0: endpoint is IN
 */
static void pch_udc_ep_fifo_flush(struct pch_udc_ep *ep, int dir)
{
	if (dir) {	/* IN ep */
		pch_udc_ep_bit_set(ep, UDC_EPCTL_ADDR, UDC_EPCTL_F);
		return;
	}
}

/**
 * pch_udc_ep_enable() - This api enables endpoint
 * @ep:		reference to structure of type pch_udc_ep_regs
 * @cfg:	current configuration information
 * @desc:	endpoint descriptor
 */
static void pch_udc_ep_enable(struct pch_udc_ep *ep,
			       struct pch_udc_cfg_data *cfg,
			       const struct usb_endpoint_descriptor *desc)
{
	u32 val = 0;
	u32 buff_size = 0;

	pch_udc_ep_set_trfr_type(ep, desc->bmAttributes);
	if (ep->in)
		buff_size = UDC_EPIN_BUFF_SIZE;
	else
		buff_size = UDC_EPOUT_BUFF_SIZE;
	pch_udc_ep_set_bufsz(ep, buff_size, ep->in);
	pch_udc_ep_set_maxpkt(ep, usb_endpoint_maxp(desc));
	pch_udc_ep_set_nak(ep);
	pch_udc_ep_fifo_flush(ep, ep->in);
	/* Configure the endpoint */
	val = ep->num << UDC_CSR_NE_NUM_SHIFT | ep->in << UDC_CSR_NE_DIR_SHIFT |
	      ((desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) <<
		UDC_CSR_NE_TYPE_SHIFT) |
	      (cfg->cur_cfg << UDC_CSR_NE_CFG_SHIFT) |
	      (cfg->cur_intf << UDC_CSR_NE_INTF_SHIFT) |
	      (cfg->cur_alt << UDC_CSR_NE_ALT_SHIFT) |
	      usb_endpoint_maxp(desc) << UDC_CSR_NE_MAX_PKT_SHIFT;

	if (ep->in)
		pch_udc_write_csr(ep->dev, val, UDC_EPIN_IDX(ep->num));
	else
		pch_udc_write_csr(ep->dev, val, UDC_EPOUT_IDX(ep->num));
}

/**
 * pch_udc_ep_disable() - This api disables endpoint
 * @ep:		reference to structure of type pch_udc_ep_regs
 */
static void pch_udc_ep_disable(struct pch_udc_ep *ep)
{
	if (ep->in) {
		/* flush the fifo */
		pch_udc_ep_writel(ep, UDC_EPCTL_F, UDC_EPCTL_ADDR);
		/* set NAK */
		pch_udc_ep_writel(ep, UDC_EPCTL_SNAK, UDC_EPCTL_ADDR);
		pch_udc_ep_bit_set(ep, UDC_EPSTS_ADDR, UDC_EPSTS_IN);
	} else {
		/* set NAK */
		pch_udc_ep_writel(ep, UDC_EPCTL_SNAK, UDC_EPCTL_ADDR);
	}
	/* reset desc pointer */
	pch_udc_ep_writel(ep, 0, UDC_DESPTR_ADDR);
}

/**
 * pch_udc_wait_ep_stall() - Wait EP stall.
 * @ep:		reference to structure of type pch_udc_ep_regs
 */
static void pch_udc_wait_ep_stall(struct pch_udc_ep *ep)
{
	unsigned int count = 10000;

	/* Wait till idle */
	while ((pch_udc_read_ep_control(ep) & UDC_EPCTL_S) && --count)
		udelay(5);
	if (!count)
		dev_err(&ep->dev->pdev->dev, "%s: wait error\n", __func__);
}

/**
 * pch_udc_init() - This API initializes usb device controller
 * @dev:	Rreference to pch_udc_regs structure
 */
static void pch_udc_init(struct pch_udc_dev *dev)
{
	if (NULL == dev) {
		pr_err("%s: Invalid address\n", __func__);
		return;
	}
	/* Soft Reset and Reset PHY */
	pch_udc_writel(dev, UDC_SRST, UDC_SRST_ADDR);
	pch_udc_writel(dev, UDC_SRST | UDC_PSRST, UDC_SRST_ADDR);
	mdelay(1);
	pch_udc_writel(dev, UDC_SRST, UDC_SRST_ADDR);
	pch_udc_writel(dev, 0x00, UDC_SRST_ADDR);
	mdelay(1);
	/* mask and clear all device interrupts */
	pch_udc_bit_set(dev, UDC_DEVIRQMSK_ADDR, UDC_DEVINT_MSK);
	pch_udc_bit_set(dev, UDC_DEVIRQSTS_ADDR, UDC_DEVINT_MSK);

	/* mask and clear all ep interrupts */
	pch_udc_bit_set(dev, UDC_EPIRQMSK_ADDR, UDC_EPINT_MSK_DISABLE_ALL);
	pch_udc_bit_set(dev, UDC_EPIRQSTS_ADDR, UDC_EPINT_MSK_DISABLE_ALL);

	/* enable dynamic CSR programmingi, self powered and device speed */
	if (speed_fs)
		pch_udc_bit_set(dev, UDC_DEVCFG_ADDR, UDC_DEVCFG_CSR_PRG |
				UDC_DEVCFG_SP | UDC_DEVCFG_SPD_FS);
	else /* defaul high speed */
		pch_udc_bit_set(dev, UDC_DEVCFG_ADDR, UDC_DEVCFG_CSR_PRG |
				UDC_DEVCFG_SP | UDC_DEVCFG_SPD_HS);
	pch_udc_bit_set(dev, UDC_DEVCTL_ADDR,
			(PCH_UDC_THLEN << UDC_DEVCTL_THLEN_SHIFT) |
			(PCH_UDC_BRLEN << UDC_DEVCTL_BRLEN_SHIFT) |
			UDC_DEVCTL_MODE | UDC_DEVCTL_BREN |
			UDC_DEVCTL_THE);
}

/**
 * pch_udc_exit() - This API exit usb device controller
 * @dev:	Reference to pch_udc_regs structure
 */
static void pch_udc_exit(struct pch_udc_dev *dev)
{
	/* mask all device interrupts */
	pch_udc_bit_set(dev, UDC_DEVIRQMSK_ADDR, UDC_DEVINT_MSK);
	/* mask all ep interrupts */
	pch_udc_bit_set(dev, UDC_EPIRQMSK_ADDR, UDC_EPINT_MSK_DISABLE_ALL);
	/* put device in disconnected state */
	pch_udc_set_disconnect(dev);
}

/**
 * pch_udc_pcd_get_frame() - This API is invoked to get the current frame number
 * @gadget:	Reference to the gadget driver
 *
 * Return codes:
 *	0:		Success
 *	-EINVAL:	If the gadget passed is NULL
 */
static int pch_udc_pcd_get_frame(struct usb_gadget *gadget)
{
	struct pch_udc_dev	*dev;

	if (!gadget)
		return -EINVAL;
	dev = container_of(gadget, struct pch_udc_dev, gadget);
	return pch_udc_get_frame(dev);
}

/**
 * pch_udc_pcd_wakeup() - This API is invoked to initiate a remote wakeup
 * @gadget:	Reference to the gadget driver
 *
 * Return codes:
 *	0:		Success
 *	-EINVAL:	If the gadget passed is NULL
 */
static int pch_udc_pcd_wakeup(struct usb_gadget *gadget)
{
	struct pch_udc_dev	*dev;
	unsigned long		flags;

	if (!gadget)
		return -EINVAL;
	dev = container_of(gadget, struct pch_udc_dev, gadget);
	spin_lock_irqsave(&dev->lock, flags);
	pch_udc_rmt_wakeup(dev);
	spin_unlock_irqrestore(&dev->lock, flags);
	return 0;
}

/**
 * pch_udc_pcd_selfpowered() - This API is invoked to specify whether the device
 *				is self powered or not
 * @gadget:	Reference to the gadget driver
 * @value:	Specifies self powered or not
 *
 * Return codes:
 *	0:		Success
 *	-EINVAL:	If the gadget passed is NULL
 */
static int pch_udc_pcd_selfpowered(struct usb_gadget *gadget, int value)
{
	struct pch_udc_dev	*dev;

	if (!gadget)
		return -EINVAL;
	gadget->is_selfpowered = (value != 0);
	dev = container_of(gadget, struct pch_udc_dev, gadget);
	if (value)
		pch_udc_set_selfpowered(dev);
	else
		pch_udc_clear_selfpowered(dev);
	return 0;
}

/**
 * pch_udc_pcd_pullup() - This API is invoked to make the device
 *				visible/invisible to the host
 * @gadget:	Reference to the gadget driver
 * @is_on:	Specifies whether the pull up is made active or inactive
 *
 * Return codes:
 *	0:		Success
 *	-EINVAL:	If the gadget passed is NULL
 */
static int pch_udc_pcd_pullup(struct usb_gadget *gadget, int is_on)
{
	struct pch_udc_dev	*dev;
	unsigned long		iflags;

	if (!gadget)
		return -EINVAL;

	dev = container_of(gadget, struct pch_udc_dev, gadget);

	spin_lock_irqsave(&dev->lock, iflags);
	if (is_on) {
		pch_udc_reconnect(dev);
	} else {
		if (dev->driver && dev->driver->disconnect) {
			spin_unlock_irqrestore(&dev->lock, iflags);
			dev->driver->disconnect(&dev->gadget);
			spin_lock_irqsave(&dev->lock, iflags);
		}
		pch_udc_set_disconnect(dev);
	}
	spin_unlock_irqrestore(&dev->lock, iflags);

	return 0;
}

/**
 * pch_udc_pcd_vbus_session() - This API is used by a driver for an external
 *				transceiver (or GPIO) that
 *				detects a VBUS power session starting/ending
 * @gadget:	Reference to the gadget driver
 * @is_active:	specifies whether the session is starting or ending
 *
 * Return codes:
 *	0:		Success
 *	-EINVAL:	If the gadget passed is NULL
 */
static int pch_udc_pcd_vbus_session(struct usb_gadget *gadget, int is_active)
{
	struct pch_udc_dev	*dev;

	if (!gadget)
		return -EINVAL;
	dev = container_of(gadget, struct pch_udc_dev, gadget);
	pch_udc_vbus_session(dev, is_active);
	return 0;
}

/**
 * pch_udc_pcd_vbus_draw() - This API is used by gadget drivers during
 *				SET_CONFIGURATION calls to
 *				specify how much power the device can consume
 * @gadget:	Reference to the gadget driver
 * @mA:		specifies the current limit in 2mA unit
 *
 * Return codes:
 *	-EINVAL:	If the gadget passed is NULL
 *	-EOPNOTSUPP:
 */
static int pch_udc_pcd_vbus_draw(struct usb_gadget *gadget, unsigned int mA)
{
	return -EOPNOTSUPP;
}

static int pch_udc_start(struct usb_gadget *g,
		struct usb_gadget_driver *driver);
static int pch_udc_stop(struct usb_gadget *g);

static const struct usb_gadget_ops pch_udc_ops = {
	.get_frame = pch_udc_pcd_get_frame,
	.wakeup = pch_udc_pcd_wakeup,
	.set_selfpowered = pch_udc_pcd_selfpowered,
	.pullup = pch_udc_pcd_pullup,
	.vbus_session = pch_udc_pcd_vbus_session,
	.vbus_draw = pch_udc_pcd_vbus_draw,
	.udc_start = pch_udc_start,
	.udc_stop = pch_udc_stop,
};

/**
 * pch_vbus_gpio_get_value() - This API gets value of GPIO port as VBUS status.
 * @dev:	Reference to the driver structure
 *
 * Return value:
 *	1: VBUS is high
 *	0: VBUS is low
 *     -1: It is not enable to detect VBUS using GPIO
 */
static int pch_vbus_gpio_get_value(struct pch_udc_dev *dev)
{
	int vbus = 0;

	if (dev->vbus_gpio.port)
		vbus = gpiod_get_value(dev->vbus_gpio.port) ? 1 : 0;
	else
		vbus = -1;

	return vbus;
}

/**
 * pch_vbus_gpio_work_fall() - This API keeps watch on VBUS becoming Low.
 *                             If VBUS is Low, disconnect is processed
 * @irq_work:	Structure for WorkQueue
 *
 */
static void pch_vbus_gpio_work_fall(struct work_struct *irq_work)
{
	struct pch_vbus_gpio_data *vbus_gpio = container_of(irq_work,
		struct pch_vbus_gpio_data, irq_work_fall);
	struct pch_udc_dev *dev =
		container_of(vbus_gpio, struct pch_udc_dev, vbus_gpio);
	int vbus_saved = -1;
	int vbus;
	int count;

	if (!dev->vbus_gpio.port)
		return;

	for (count = 0; count < (PCH_VBUS_PERIOD / PCH_VBUS_INTERVAL);
		count++) {
		vbus = pch_vbus_gpio_get_value(dev);

		if ((vbus_saved == vbus) && (vbus == 0)) {
			dev_dbg(&dev->pdev->dev, "VBUS fell");
			if (dev->driver
				&& dev->driver->disconnect) {
				dev->driver->disconnect(
					&dev->gadget);
			}
			if (dev->vbus_gpio.intr)
				pch_udc_init(dev);
			else
				pch_udc_reconnect(dev);
			return;
		}
		vbus_saved = vbus;
		mdelay(PCH_VBUS_INTERVAL);
	}
}

/**
 * pch_vbus_gpio_work_rise() - This API checks VBUS is High.
 *                             If VBUS is High, connect is processed
 * @irq_work:	Structure for WorkQueue
 *
 */
static void pch_vbus_gpio_work_rise(struct work_struct *irq_work)
{
	struct pch_vbus_gpio_data *vbus_gpio = container_of(irq_work,
		struct pch_vbus_gpio_data, irq_work_rise);
	struct pch_udc_dev *dev =
		container_of(vbus_gpio, struct pch_udc_dev, vbus_gpio);
	int vbus;

	if (!dev->vbus_gpio.port)
		return;

	mdelay(PCH_VBUS_INTERVAL);
	vbus = pch_vbus_gpio_get_value(dev);

	if (vbus == 1) {
		dev_dbg(&dev->pdev->dev, "VBUS rose");
		pch_udc_reconnect(dev);
		return;
	}
}

/**
 * pch_vbus_gpio_irq() - IRQ handler for GPIO interrupt for changing VBUS
 * @irq:	Interrupt request number
 * @data:	Reference to the device structure
 *
 * Return codes:
 *	0: Success
 *	-EINVAL: GPIO port is invalid or can't be initialized.
 */
static irqreturn_t pch_vbus_gpio_irq(int irq, void *data)
{
	struct pch_udc_dev *dev = (struct pch_udc_dev *)data;

	if (!dev->vbus_gpio.port || !dev->vbus_gpio.intr)
		return IRQ_NONE;

	if (pch_vbus_gpio_get_value(dev))
		schedule_work(&dev->vbus_gpio.irq_work_rise);
	else
		schedule_work(&dev->vbus_gpio.irq_work_fall);

	return IRQ_HANDLED;
}

/**
 * pch_vbus_gpio_init() - This API initializes GPIO port detecting VBUS.
 * @dev:		Reference to the driver structure
 *
 * Return codes:
 *	0: Success
 *	-EINVAL: GPIO port is invalid or can't be initialized.
 */
static int pch_vbus_gpio_init(struct pch_udc_dev *dev)
{
	struct device *d = &dev->pdev->dev;
	int err;
	int irq_num = 0;
	struct gpio_desc *gpiod;

	dev->vbus_gpio.port = NULL;
	dev->vbus_gpio.intr = 0;

	/* Retrieve the GPIO line from the USB gadget device */
	gpiod = devm_gpiod_get_optional(d, NULL, GPIOD_IN);
	if (IS_ERR(gpiod))
		return PTR_ERR(gpiod);
	gpiod_set_consumer_name(gpiod, "pch_vbus");

	dev->vbus_gpio.port = gpiod;
	INIT_WORK(&dev->vbus_gpio.irq_work_fall, pch_vbus_gpio_work_fall);

	irq_num = gpiod_to_irq(gpiod);
	if (irq_num > 0) {
		irq_set_irq_type(irq_num, IRQ_TYPE_EDGE_BOTH);
		err = request_irq(irq_num, pch_vbus_gpio_irq, 0,
			"vbus_detect", dev);
		if (!err) {
			dev->vbus_gpio.intr = irq_num;
			INIT_WORK(&dev->vbus_gpio.irq_work_rise,
				pch_vbus_gpio_work_rise);
		} else {
			pr_err("%s: can't request irq %d, err: %d\n",
				__func__, irq_num, err);
		}
	}

	return 0;
}

/**
 * pch_vbus_gpio_free() - This API frees resources of GPIO port
 * @dev:	Reference to the driver structure
 */
static void pch_vbus_gpio_free(struct pch_udc_dev *dev)
{
	if (dev->vbus_gpio.intr)
		free_irq(dev->vbus_gpio.intr, dev);
}

/**
 * complete_req() - This API is invoked from the driver when processing
 *			of a request is complete
 * @ep:		Reference to the endpoint structure
 * @req:	Reference to the request structure
 * @status:	Indicates the success/failure of completion
 */
static void complete_req(struct pch_udc_ep *ep, struct pch_udc_request *req,
								 int status)
	__releases(&dev->lock)
	__acquires(&dev->lock)
{
	struct pch_udc_dev	*dev;
	unsigned halted = ep->halted;

	list_del_init(&req->queue);

	/* set new status if pending */
	if (req->req.status == -EINPROGRESS)
		req->req.status = status;
	else
		status = req->req.status;

	dev = ep->dev;
	usb_gadget_unmap_request(&dev->gadget, &req->req, ep->in);
	ep->halted = 1;
	spin_unlock(&dev->lock);
	if (!ep->in)
		pch_udc_ep_clear_rrdy(ep);
	usb_gadget_giveback_request(&ep->ep, &req->req);
	spin_lock(&dev->lock);
	ep->halted = halted;
}

/**
 * empty_req_queue() - This API empties the request queue of an endpoint
 * @ep:		Reference to the endpoint structure
 */
static void empty_req_queue(struct pch_udc_ep *ep)
{
	struct pch_udc_request	*req;

	ep->halted = 1;
	while (!list_empty(&ep->queue)) {
		req = list_entry(ep->queue.next, struct pch_udc_request, queue);
		complete_req(ep, req, -ESHUTDOWN);	/* Remove from list */
	}
}

/**
 * pch_udc_free_dma_chain() - This function frees the DMA chain created
 *				for the request
 * @dev:	Reference to the driver structure
 * @req:	Reference to the request to be freed
 *
 * Return codes:
 *	0: Success
 */
static void pch_udc_free_dma_chain(struct pch_udc_dev *dev,
				   struct pch_udc_request *req)
{
	struct pch_udc_data_dma_desc *td = req->td_data;
	unsigned i = req->chain_len;

	dma_addr_t addr2;
	dma_addr_t addr = (dma_addr_t)td->next;
	td->next = 0x00;
	for (; i > 1; --i) {
		/* do not free first desc., will be done by free for request */
		td = phys_to_virt(addr);
		addr2 = (dma_addr_t)td->next;
		dma_pool_free(dev->data_requests, td, addr);
		addr = addr2;
	}
	req->chain_len = 1;
}

/**
 * pch_udc_create_dma_chain() - This function creates or reinitializes
 *				a DMA chain
 * @ep:		Reference to the endpoint structure
 * @req:	Reference to the request
 * @buf_len:	The buffer length
 * @gfp_flags:	Flags to be used while mapping the data buffer
 *
 * Return codes:
 *	0:		success,
 *	-ENOMEM:	dma_pool_alloc invocation fails
 */
static int pch_udc_create_dma_chain(struct pch_udc_ep *ep,
				    struct pch_udc_request *req,
				    unsigned long buf_len,
				    gfp_t gfp_flags)
{
	struct pch_udc_data_dma_desc *td = req->td_data, *last;
	unsigned long bytes = req->req.length, i = 0;
	dma_addr_t dma_addr;
	unsigned len = 1;

	if (req->chain_len > 1)
		pch_udc_free_dma_chain(ep->dev, req);

	td->dataptr = req->req.dma;
	td->status = PCH_UDC_BS_HST_BSY;

	for (; ; bytes -= buf_len, ++len) {
		td->status = PCH_UDC_BS_HST_BSY | min(buf_len, bytes);
		if (bytes <= buf_len)
			break;
		last = td;
		td = dma_pool_alloc(ep->dev->data_requests, gfp_flags,
				    &dma_addr);
		if (!td)
			goto nomem;
		i += buf_len;
		td->dataptr = req->td_data->dataptr + i;
		last->next = dma_addr;
	}

	req->td_data_last = td;
	td->status |= PCH_UDC_DMA_LAST;
	td->next = req->td_data_phys;
	req->chain_len = len;
	return 0;

nomem:
	if (len > 1) {
		req->chain_len = len;
		pch_udc_free_dma_chain(ep->dev, req);
	}
	req->chain_len = 1;
	return -ENOMEM;
}

/**
 * prepare_dma() - This function creates and initializes the DMA chain
 *			for the request
 * @ep:		Reference to the endpoint structure
 * @req:	Reference to the request
 * @gfp:	Flag to be used while mapping the data buffer
 *
 * Return codes:
 *	0:		Success
 *	Other 0:	linux error number on failure
 */
static int prepare_dma(struct pch_udc_ep *ep, struct pch_udc_request *req,
			  gfp_t gfp)
{
	int	retval;

	/* Allocate and create a DMA chain */
	retval = pch_udc_create_dma_chain(ep, req, ep->ep.maxpacket, gfp);
	if (retval) {
		pr_err("%s: could not create DMA chain:%d\n", __func__, retval);
		return retval;
	}
	if (ep->in)
		req->td_data->status = (req->td_data->status &
				~PCH_UDC_BUFF_STS) | PCH_UDC_BS_HST_RDY;
	return 0;
}

/**
 * process_zlp() - This function process zero length packets
 *			from the gadget driver
 * @ep:		Reference to the endpoint structure
 * @req:	Reference to the request
 */
static void process_zlp(struct pch_udc_ep *ep, struct pch_udc_request *req)
{
	struct pch_udc_dev	*dev = ep->dev;

	/* IN zlp's are handled by hardware */
	complete_req(ep, req, 0);

	/* if set_config or set_intf is waiting for ack by zlp
	 * then set CSR_DONE
	 */
	if (dev->set_cfg_not_acked) {
		pch_udc_set_csr_done(dev);
		dev->set_cfg_not_acked = 0;
	}
	/* setup command is ACK'ed now by zlp */
	if (!dev->stall && dev->waiting_zlp_ack) {
		pch_udc_ep_clear_nak(&(dev->ep[UDC_EP0IN_IDX]));
		dev->waiting_zlp_ack = 0;
	}
}

/**
 * pch_udc_start_rxrequest() - This function starts the receive requirement.
 * @ep:		Reference to the endpoint structure
 * @req:	Reference to the request structure
 */
static void pch_udc_start_rxrequest(struct pch_udc_ep *ep,
					 struct pch_udc_request *req)
{
	struct pch_udc_data_dma_desc *td_data;

	pch_udc_clear_dma(ep->dev, DMA_DIR_RX);
	td_data = req->td_data;
	/* Set the status bits for all descriptors */
	while (1) {
		td_data->status = (td_data->status & ~PCH_UDC_BUFF_STS) |
				    PCH_UDC_BS_HST_RDY;
		if ((td_data->status & PCH_UDC_DMA_LAST) ==  PCH_UDC_DMA_LAST)
			break;
		td_data = phys_to_virt(td_data->next);
	}
	/* Write the descriptor pointer */
	pch_udc_ep_set_ddptr(ep, req->td_data_phys);
	req->dma_going = 1;
	pch_udc_enable_ep_interrupts(ep->dev, UDC_EPINT_OUT_EP0 << ep->num);
	pch_udc_set_dma(ep->dev, DMA_DIR_RX);
	pch_udc_ep_clear_nak(ep);
	pch_udc_ep_set_rrdy(ep);
}

/**
 * pch_udc_pcd_ep_enable() - This API enables the endpoint. It is called
 *				from gadget driver
 * @usbep:	Reference to the USB endpoint structure
 * @desc:	Reference to the USB endpoint descriptor structure
 *
 * Return codes:
 *	0:		Success
 *	-EINVAL:
 *	-ESHUTDOWN:
 */
static int pch_udc_pcd_ep_enable(struct usb_ep *usbep,
				    const struct usb_endpoint_descriptor *desc)
{
	struct pch_udc_ep	*ep;
	struct pch_udc_dev	*dev;
	unsigned long		iflags;

	if (!usbep || (usbep->name == ep0_string) || !desc ||
	    (desc->bDescriptorType != USB_DT_ENDPOINT) || !desc->wMaxPacketSize)
		return -EINVAL;

	ep = container_of(usbep, struct pch_udc_ep, ep);
	dev = ep->dev;
	if (!dev->driver || (dev->gadget.speed == USB_SPEED_UNKNOWN))
		return -ESHUTDOWN;
	spin_lock_irqsave(&dev->lock, iflags);
	ep->ep.desc = desc;
	ep->halted = 0;
	pch_udc_ep_enable(ep, &ep->dev->cfg_data, desc);
	ep->ep.maxpacket = usb_endpoint_maxp(desc);
	pch_udc_enable_ep_interrupts(ep->dev, PCH_UDC_EPINT(ep->in, ep->num));
	spin_unlock_irqrestore(&dev->lock, iflags);
	return 0;
}

/**
 * pch_udc_pcd_ep_disable() - This API disables endpoint and is called
 *				from gadget driver
 * @usbep:	Reference to the USB endpoint structure
 *
 * Return codes:
 *	0:		Success
 *	-EINVAL:
 */
static int pch_udc_pcd_ep_disable(struct usb_ep *usbep)
{
	struct pch_udc_ep	*ep;
	unsigned long	iflags;

	if (!usbep)
		return -EINVAL;

	ep = container_of(usbep, struct pch_udc_ep, ep);
	if ((usbep->name == ep0_string) || !ep->ep.desc)
		return -EINVAL;

	spin_lock_irqsave(&ep->dev->lock, iflags);
	empty_req_queue(ep);
	ep->halted = 1;
	pch_udc_ep_disable(ep);
	pch_udc_disable_ep_interrupts(ep->dev, PCH_UDC_EPINT(ep->in, ep->num));
	ep->ep.desc = NULL;
	INIT_LIST_HEAD(&ep->queue);
	spin_unlock_irqrestore(&ep->dev->lock, iflags);
	return 0;
}

/**
 * pch_udc_alloc_request() - This function allocates request structure.
 *				It is called by gadget driver
 * @usbep:	Reference to the USB endpoint structure
 * @gfp:	Flag to be used while allocating memory
 *
 * Return codes:
 *	NULL:			Failure
 *	Allocated address:	Success
 */
static struct usb_request *pch_udc_alloc_request(struct usb_ep *usbep,
						  gfp_t gfp)
{
	struct pch_udc_request		*req;
	struct pch_udc_ep		*ep;
	struct pch_udc_data_dma_desc	*dma_desc;

	if (!usbep)
		return NULL;
	ep = container_of(usbep, struct pch_udc_ep, ep);
	req = kzalloc(sizeof *req, gfp);
	if (!req)
		return NULL;
	req->req.dma = DMA_ADDR_INVALID;
	INIT_LIST_HEAD(&req->queue);
	if (!ep->dev->dma_addr)
		return &req->req;
	/* ep0 in requests are allocated from data pool here */
	dma_desc = dma_pool_alloc(ep->dev->data_requests, gfp,
				  &req->td_data_phys);
	if (NULL == dma_desc) {
		kfree(req);
		return NULL;
	}
	/* prevent from using desc. - set HOST BUSY */
	dma_desc->status |= PCH_UDC_BS_HST_BSY;
	dma_desc->dataptr = lower_32_bits(DMA_ADDR_INVALID);
	req->td_data = dma_desc;
	req->td_data_last = dma_desc;
	req->chain_len = 1;
	return &req->req;
}

/**
 * pch_udc_free_request() - This function frees request structure.
 *				It is called by gadget driver
 * @usbep:	Reference to the USB endpoint structure
 * @usbreq:	Reference to the USB request
 */
static void pch_udc_free_request(struct usb_ep *usbep,
				  struct usb_request *usbreq)
{
	struct pch_udc_ep	*ep;
	struct pch_udc_request	*req;
	struct pch_udc_dev	*dev;

	if (!usbep || !usbreq)
		return;
	ep = container_of(usbep, struct pch_udc_ep, ep);
	req = container_of(usbreq, struct pch_udc_request, req);
	dev = ep->dev;
	if (!list_empty(&req->queue))
		dev_err(&dev->pdev->dev, "%s: %s req=0x%p queue not empty\n",
			__func__, usbep->name, req);
	if (req->td_data != NULL) {
		if (req->chain_len > 1)
			pch_udc_free_dma_chain(ep->dev, req);
		dma_pool_free(ep->dev->data_requests, req->td_data,
			      req->td_data_phys);
	}
	kfree(req);
}

/**
 * pch_udc_pcd_queue() - This function queues a request packet. It is called
 *			by gadget driver
 * @usbep:	Reference to the USB endpoint structure
 * @usbreq:	Reference to the USB request
 * @gfp:	Flag to be used while mapping the data buffer
 *
 * Return codes:
 *	0:			Success
 *	linux error number:	Failure
 */
static int pch_udc_pcd_queue(struct usb_ep *usbep, struct usb_request *usbreq,
								 gfp_t gfp)
{
	int retval = 0;
	struct pch_udc_ep	*ep;
	struct pch_udc_dev	*dev;
	struct pch_udc_request	*req;
	unsigned long	iflags;

	if (!usbep || !usbreq || !usbreq->complete || !usbreq->buf)
		return -EINVAL;
	ep = container_of(usbep, struct pch_udc_ep, ep);
	dev = ep->dev;
	if (!ep->ep.desc && ep->num)
		return -EINVAL;
	req = container_of(usbreq, struct pch_udc_request, req);
	if (!list_empty(&req->queue))
		return -EINVAL;
	if (!dev->driver || (dev->gadget.speed == USB_SPEED_UNKNOWN))
		return -ESHUTDOWN;
	spin_lock_irqsave(&dev->lock, iflags);
	/* map the buffer for dma */
	retval = usb_gadget_map_request(&dev->gadget, usbreq, ep->in);
	if (retval)
		goto probe_end;
	if (usbreq->length > 0) {
		retval = prepare_dma(ep, req, GFP_ATOMIC);
		if (retval)
			goto probe_end;
	}
	usbreq->actual = 0;
	usbreq->status = -EINPROGRESS;
	req->dma_done = 0;
	if (list_empty(&ep->queue) && !ep->halted) {
		/* no pending transfer, so start this req */
		if (!usbreq->length) {
			process_zlp(ep, req);
			retval = 0;
			goto probe_end;
		}
		if (!ep->in) {
			pch_udc_start_rxrequest(ep, req);
		} else {
			/*
			* For IN trfr the descriptors will be programmed and
			* P bit will be set when
			* we get an IN token
			*/
			pch_udc_wait_ep_stall(ep);
			pch_udc_ep_clear_nak(ep);
			pch_udc_enable_ep_interrupts(ep->dev, (1 << ep->num));
		}
	}
	/* Now add this request to the ep's pending requests */
	if (req != NULL)
		list_add_tail(&req->queue, &ep->queue);

probe_end:
	spin_unlock_irqrestore(&dev->lock, iflags);
	return retval;
}

/**
 * pch_udc_pcd_dequeue() - This function de-queues a request packet.
 *				It is called by gadget driver
 * @usbep:	Reference to the USB endpoint structure
 * @usbreq:	Reference to the USB request
 *
 * Return codes:
 *	0:			Success
 *	linux error number:	Failure
 */
static int pch_udc_pcd_dequeue(struct usb_ep *usbep,
				struct usb_request *usbreq)
{
	struct pch_udc_ep	*ep;
	struct pch_udc_request	*req;
	unsigned long		flags;
	int ret = -EINVAL;

	ep = container_of(usbep, struct pch_udc_ep, ep);
	if (!usbep || !usbreq || (!ep->ep.desc && ep->num))
		return ret;
	req = container_of(usbreq, struct pch_udc_request, req);
	spin_lock_irqsave(&ep->dev->lock, flags);
	/* make sure it's still queued on this endpoint */
	list_for_each_entry(req, &ep->queue, queue) {
		if (&req->req == usbreq) {
			pch_udc_ep_set_nak(ep);
			if (!list_empty(&req->queue))
				complete_req(ep, req, -ECONNRESET);
			ret = 0;
			break;
		}
	}
	spin_unlock_irqrestore(&ep->dev->lock, flags);
	return ret;
}

/**
 * pch_udc_pcd_set_halt() - This function Sets or clear the endpoint halt
 *			    feature
 * @usbep:	Reference to the USB endpoint structure
 * @halt:	Specifies whether to set or clear the feature
 *
 * Return codes:
 *	0:			Success
 *	linux error number:	Failure
 */
static int pch_udc_pcd_set_halt(struct usb_ep *usbep, int halt)
{
	struct pch_udc_ep	*ep;
	unsigned long iflags;
	int ret;

	if (!usbep)
		return -EINVAL;
	ep = container_of(usbep, struct pch_udc_ep, ep);
	if (!ep->ep.desc && !ep->num)
		return -EINVAL;
	if (!ep->dev->driver || (ep->dev->gadget.speed == USB_SPEED_UNKNOWN))
		return -ESHUTDOWN;
	spin_lock_irqsave(&udc_stall_spinlock, iflags);
	if (list_empty(&ep->queue)) {
		if (halt) {
			if (ep->num == PCH_UDC_EP0)
				ep->dev->stall = 1;
			pch_udc_ep_set_stall(ep);
			pch_udc_enable_ep_interrupts(
				ep->dev, PCH_UDC_EPINT(ep->in, ep->num));
		} else {
			pch_udc_ep_clear_stall(ep);
		}
		ret = 0;
	} else {
		ret = -EAGAIN;
	}
	spin_unlock_irqrestore(&udc_stall_spinlock, iflags);
	return ret;
}

/**
 * pch_udc_pcd_set_wedge() - This function Sets or clear the endpoint
 *				halt feature
 * @usbep:	Reference to the USB endpoint structure
 *
 * Return codes:
 *	0:			Success
 *	linux error number:	Failure
 */
static int pch_udc_pcd_set_wedge(struct usb_ep *usbep)
{
	struct pch_udc_ep	*ep;
	unsigned long iflags;
	int ret;

	if (!usbep)
		return -EINVAL;
	ep = container_of(usbep, struct pch_udc_ep, ep);
	if (!ep->ep.desc && !ep->num)
		return -EINVAL;
	if (!ep->dev->driver || (ep->dev->gadget.speed == USB_SPEED_UNKNOWN))
		return -ESHUTDOWN;
	spin_lock_irqsave(&udc_stall_spinlock, iflags);
	if (!list_empty(&ep->queue)) {
		ret = -EAGAIN;
	} else {
		if (ep->num == PCH_UDC_EP0)
			ep->dev->stall = 1;
		pch_udc_ep_set_stall(ep);
		pch_udc_enable_ep_interrupts(ep->dev,
					     PCH_UDC_EPINT(ep->in, ep->num));
		ep->dev->prot_stall = 1;
		ret = 0;
	}
	spin_unlock_irqrestore(&udc_stall_spinlock, iflags);
	return ret;
}

/**
 * pch_udc_pcd_fifo_flush() - This function Flush the FIFO of specified endpoint
 * @usbep:	Reference to the USB endpoint structure
 */
static void pch_udc_pcd_fifo_flush(struct usb_ep *usbep)
{
	struct pch_udc_ep  *ep;

	if (!usbep)
		return;

	ep = container_of(usbep, struct pch_udc_ep, ep);
	if (ep->ep.desc || !ep->num)
		pch_udc_ep_fifo_flush(ep, ep->in);
}

static const struct usb_ep_ops pch_udc_ep_ops = {
	.enable		= pch_udc_pcd_ep_enable,
	.disable	= pch_udc_pcd_ep_disable,
	.alloc_request	= pch_udc_alloc_request,
	.free_request	= pch_udc_free_request,
	.queue		= pch_udc_pcd_queue,
	.dequeue	= pch_udc_pcd_dequeue,
	.set_halt	= pch_udc_pcd_set_halt,
	.set_wedge	= pch_udc_pcd_set_wedge,
	.fifo_status	= NULL,
	.fifo_flush	= pch_udc_pcd_fifo_flush,
};

/**
 * pch_udc_init_setup_buff() - This function initializes the SETUP buffer
 * @td_stp:	Reference to the SETP buffer structure
 */
static void pch_udc_init_setup_buff(struct pch_udc_stp_dma_desc *td_stp)
{
	static u32	pky_marker;

	if (!td_stp)
		return;
	td_stp->reserved = ++pky_marker;
	memset(&td_stp->request, 0xFF, sizeof td_stp->request);
	td_stp->status = PCH_UDC_BS_HST_RDY;
}

/**
 * pch_udc_start_next_txrequest() - This function starts
 *					the next transmission requirement
 * @ep:	Reference to the endpoint structure
 */
static void pch_udc_start_next_txrequest(struct pch_udc_ep *ep)
{
	struct pch_udc_request *req;
	struct pch_udc_data_dma_desc *td_data;

	if (pch_udc_read_ep_control(ep) & UDC_EPCTL_P)
		return;

	if (list_empty(&ep->queue))
		return;

	/* next request */
	req = list_entry(ep->queue.next, struct pch_udc_request, queue);
	if (req->dma_going)
		return;
	if (!req->td_data)
		return;
	pch_udc_wait_ep_stall(ep);
	req->dma_going = 1;
	pch_udc_ep_set_ddptr(ep, 0);
	td_data = req->td_data;
	while (1) {
		td_data->status = (td_data->status & ~PCH_UDC_BUFF_STS) |
				   PCH_UDC_BS_HST_RDY;
		if ((td_data->status & PCH_UDC_DMA_LAST) == PCH_UDC_DMA_LAST)
			break;
		td_data = phys_to_virt(td_data->next);
	}
	pch_udc_ep_set_ddptr(ep, req->td_data_phys);
	pch_udc_set_dma(ep->dev, DMA_DIR_TX);
	pch_udc_ep_set_pd(ep);
	pch_udc_enable_ep_interrupts(ep->dev, PCH_UDC_EPINT(ep->in, ep->num));
	pch_udc_ep_clear_nak(ep);
}

/**
 * pch_udc_complete_transfer() - This function completes a transfer
 * @ep:		Reference to the endpoint structure
 */
static void pch_udc_complete_transfer(struct pch_udc_ep *ep)
{
	struct pch_udc_request *req;
	struct pch_udc_dev *dev = ep->dev;

	if (list_empty(&ep->queue))
		return;
	req = list_entry(ep->queue.next, struct pch_udc_request, queue);
	if ((req->td_data_last->status & PCH_UDC_BUFF_STS) !=
	    PCH_UDC_BS_DMA_DONE)
		return;
	if ((req->td_data_last->status & PCH_UDC_RXTX_STS) !=
	     PCH_UDC_RTS_SUCC) {
		dev_err(&dev->pdev->dev, "Invalid RXTX status (0x%08x) "
			"epstatus=0x%08x\n",
		       (req->td_data_last->status & PCH_UDC_RXTX_STS),
		       (int)(ep->epsts));
		return;
	}

	req->req.actual = req->req.length;
	req->td_data_last->status = PCH_UDC_BS_HST_BSY | PCH_UDC_DMA_LAST;
	req->td_data->status = PCH_UDC_BS_HST_BSY | PCH_UDC_DMA_LAST;
	complete_req(ep, req, 0);
	req->dma_going = 0;
	if (!list_empty(&ep->queue)) {
		pch_udc_wait_ep_stall(ep);
		pch_udc_ep_clear_nak(ep);
		pch_udc_enable_ep_interrupts(ep->dev,
					     PCH_UDC_EPINT(ep->in, ep->num));
	} else {
		pch_udc_disable_ep_interrupts(ep->dev,
					      PCH_UDC_EPINT(ep->in, ep->num));
	}
}

/**
 * pch_udc_complete_receiver() - This function completes a receiver
 * @ep:		Reference to the endpoint structure
 */
static void pch_udc_complete_receiver(struct pch_udc_ep *ep)
{
	struct pch_udc_request *req;
	struct pch_udc_dev *dev = ep->dev;
	unsigned int count;
	struct pch_udc_data_dma_desc *td;
	dma_addr_t addr;

	if (list_empty(&ep->queue))
		return;
	/* next request */
	req = list_entry(ep->queue.next, struct pch_udc_request, queue);
	pch_udc_clear_dma(ep->dev, DMA_DIR_RX);
	pch_udc_ep_set_ddptr(ep, 0);
	if ((req->td_data_last->status & PCH_UDC_BUFF_STS) ==
	    PCH_UDC_BS_DMA_DONE)
		td = req->td_data_last;
	else
		td = req->td_data;

	while (1) {
		if ((td->status & PCH_UDC_RXTX_STS) != PCH_UDC_RTS_SUCC) {
			dev_err(&dev->pdev->dev, "Invalid RXTX status=0x%08x "
				"epstatus=0x%08x\n",
				(req->td_data->status & PCH_UDC_RXTX_STS),
				(int)(ep->epsts));
			return;
		}
		if ((td->status & PCH_UDC_BUFF_STS) == PCH_UDC_BS_DMA_DONE)
			if (td->status & PCH_UDC_DMA_LAST) {
				count = td->status & PCH_UDC_RXTX_BYTES;
				break;
			}
		if (td == req->td_data_last) {
			dev_err(&dev->pdev->dev, "Not complete RX descriptor");
			return;
		}
		addr = (dma_addr_t)td->next;
		td = phys_to_virt(addr);
	}
	/* on 64k packets the RXBYTES field is zero */
	if (!count && (req->req.length == UDC_DMA_MAXPACKET))
		count = UDC_DMA_MAXPACKET;
	req->td_data->status |= PCH_UDC_DMA_LAST;
	td->status |= PCH_UDC_BS_HST_BSY;

	req->dma_going = 0;
	req->req.actual = count;
	complete_req(ep, req, 0);
	/* If there is a new/failed requests try that now */
	if (!list_empty(&ep->queue)) {
		req = list_entry(ep->queue.next, struct pch_udc_request, queue);
		pch_udc_start_rxrequest(ep, req);
	}
}

/**
 * pch_udc_svc_data_in() - This function process endpoint interrupts
 *				for IN endpoints
 * @dev:	Reference to the device structure
 * @ep_num:	Endpoint that generated the interrupt
 */
static void pch_udc_svc_data_in(struct pch_udc_dev *dev, int ep_num)
{
	u32	epsts;
	struct pch_udc_ep	*ep;

	ep = &dev->ep[UDC_EPIN_IDX(ep_num)];
	epsts = ep->epsts;
	ep->epsts = 0;

	if (!(epsts & (UDC_EPSTS_IN | UDC_EPSTS_BNA  | UDC_EPSTS_HE |
		       UDC_EPSTS_TDC | UDC_EPSTS_RCS | UDC_EPSTS_TXEMPTY |
		       UDC_EPSTS_RSS | UDC_EPSTS_XFERDONE)))
		return;
	if ((epsts & UDC_EPSTS_BNA))
		return;
	if (epsts & UDC_EPSTS_HE)
		return;
	if (epsts & UDC_EPSTS_RSS) {
		pch_udc_ep_set_stall(ep);
		pch_udc_enable_ep_interrupts(ep->dev,
					     PCH_UDC_EPINT(ep->in, ep->num));
	}
	if (epsts & UDC_EPSTS_RCS) {
		if (!dev->prot_stall) {
			pch_udc_ep_clear_stall(ep);
		} else {
			pch_udc_ep_set_stall(ep);
			pch_udc_enable_ep_interrupts(ep->dev,
						PCH_UDC_EPINT(ep->in, ep->num));
		}
	}
	if (epsts & UDC_EPSTS_TDC)
		pch_udc_complete_transfer(ep);
	/* On IN interrupt, provide data if we have any */
	if ((epsts & UDC_EPSTS_IN) && !(epsts & UDC_EPSTS_RSS) &&
	    !(epsts & UDC_EPSTS_TDC) && !(epsts & UDC_EPSTS_TXEMPTY))
		pch_udc_start_next_txrequest(ep);
}

/**
 * pch_udc_svc_data_out() - Handles interrupts from OUT endpoint
 * @dev:	Reference to the device structure
 * @ep_num:	Endpoint that generated the interrupt
 */
static void pch_udc_svc_data_out(struct pch_udc_dev *dev, int ep_num)
{
	u32			epsts;
	struct pch_udc_ep		*ep;
	struct pch_udc_request		*req = NULL;

	ep = &dev->ep[UDC_EPOUT_IDX(ep_num)];
	epsts = ep->epsts;
	ep->epsts = 0;

	if ((epsts & UDC_EPSTS_BNA) && (!list_empty(&ep->queue))) {
		/* next request */
		req = list_entry(ep->queue.next, struct pch_udc_request,
				 queue);
		if ((req->td_data_last->status & PCH_UDC_BUFF_STS) !=
		     PCH_UDC_BS_DMA_DONE) {
			if (!req->dma_going)
				pch_udc_start_rxrequest(ep, req);
			return;
		}
	}
	if (epsts & UDC_EPSTS_HE)
		return;
	if (epsts & UDC_EPSTS_RSS) {
		pch_udc_ep_set_stall(ep);
		pch_udc_enable_ep_interrupts(ep->dev,
					     PCH_UDC_EPINT(ep->in, ep->num));
	}
	if (epsts & UDC_EPSTS_RCS) {
		if (!dev->prot_stall) {
			pch_udc_ep_clear_stall(ep);
		} else {
			pch_udc_ep_set_stall(ep);
			pch_udc_enable_ep_interrupts(ep->dev,
						PCH_UDC_EPINT(ep->in, ep->num));
		}
	}
	if (((epsts & UDC_EPSTS_OUT_MASK) >> UDC_EPSTS_OUT_SHIFT) ==
	    UDC_EPSTS_OUT_DATA) {
		if (ep->dev->prot_stall == 1) {
			pch_udc_ep_set_stall(ep);
			pch_udc_enable_ep_interrupts(ep->dev,
						PCH_UDC_EPINT(ep->in, ep->num));
		} else {
			pch_udc_complete_receiver(ep);
		}
	}
	if (list_empty(&ep->queue))
		pch_udc_set_dma(dev, DMA_DIR_RX);
}

static int pch_udc_gadget_setup(struct pch_udc_dev *dev)
	__must_hold(&dev->lock)
{
	int rc;

	/* In some cases we can get an interrupt before driver gets setup */
	if (!dev->driver)
		return -ESHUTDOWN;

	spin_unlock(&dev->lock);
	rc = dev->driver->setup(&dev->gadget, &dev->setup_data);
	spin_lock(&dev->lock);
	return rc;
}

/**
 * pch_udc_svc_control_in() - Handle Control IN endpoint interrupts
 * @dev:	Reference to the device structure
 */
static void pch_udc_svc_control_in(struct pch_udc_dev *dev)
{
	u32	epsts;
	struct pch_udc_ep	*ep;
	struct pch_udc_ep	*ep_out;

	ep = &dev->ep[UDC_EP0IN_IDX];
	ep_out = &dev->ep[UDC_EP0OUT_IDX];
	epsts = ep->epsts;
	ep->epsts = 0;

	if (!(epsts & (UDC_EPSTS_IN | UDC_EPSTS_BNA | UDC_EPSTS_HE |
		       UDC_EPSTS_TDC | UDC_EPSTS_RCS | UDC_EPSTS_TXEMPTY |
		       UDC_EPSTS_XFERDONE)))
		return;
	if ((epsts & UDC_EPSTS_BNA))
		return;
	if (epsts & UDC_EPSTS_HE)
		return;
	if ((epsts & UDC_EPSTS_TDC) && (!dev->stall)) {
		pch_udc_complete_transfer(ep);
		pch_udc_clear_dma(dev, DMA_DIR_RX);
		ep_out->td_data->status = (ep_out->td_data->status &
					~PCH_UDC_BUFF_STS) |
					PCH_UDC_BS_HST_RDY;
		pch_udc_ep_clear_nak(ep_out);
		pch_udc_set_dma(dev, DMA_DIR_RX);
		pch_udc_ep_set_rrdy(ep_out);
	}
	/* On IN interrupt, provide data if we have any */
	if ((epsts & UDC_EPSTS_IN) && !(epsts & UDC_EPSTS_TDC) &&
	     !(epsts & UDC_EPSTS_TXEMPTY))
		pch_udc_start_next_txrequest(ep);
}

/**
 * pch_udc_svc_control_out() - Routine that handle Control
 *					OUT endpoint interrupts
 * @dev:	Reference to the device structure
 */
static void pch_udc_svc_control_out(struct pch_udc_dev *dev)
	__releases(&dev->lock)
	__acquires(&dev->lock)
{
	u32	stat;
	int setup_supported;
	struct pch_udc_ep	*ep;

	ep = &dev->ep[UDC_EP0OUT_IDX];
	stat = ep->epsts;
	ep->epsts = 0;

	/* If setup data */
	if (((stat & UDC_EPSTS_OUT_MASK) >> UDC_EPSTS_OUT_SHIFT) ==
	    UDC_EPSTS_OUT_SETUP) {
		dev->stall = 0;
		dev->ep[UDC_EP0IN_IDX].halted = 0;
		dev->ep[UDC_EP0OUT_IDX].halted = 0;
		dev->setup_data = ep->td_stp->request;
		pch_udc_init_setup_buff(ep->td_stp);
		pch_udc_clear_dma(dev, DMA_DIR_RX);
		pch_udc_ep_fifo_flush(&(dev->ep[UDC_EP0IN_IDX]),
				      dev->ep[UDC_EP0IN_IDX].in);
		if ((dev->setup_data.bRequestType & USB_DIR_IN))
			dev->gadget.ep0 = &dev->ep[UDC_EP0IN_IDX].ep;
		else /* OUT */
			dev->gadget.ep0 = &ep->ep;
		/* If Mass storage Reset */
		if ((dev->setup_data.bRequestType == 0x21) &&
		    (dev->setup_data.bRequest == 0xFF))
			dev->prot_stall = 0;
		/* call gadget with setup data received */
		setup_supported = pch_udc_gadget_setup(dev);

		if (dev->setup_data.bRequestType & USB_DIR_IN) {
			ep->td_data->status = (ep->td_data->status &
						~PCH_UDC_BUFF_STS) |
						PCH_UDC_BS_HST_RDY;
			pch_udc_ep_set_ddptr(ep, ep->td_data_phys);
		}
		/* ep0 in returns data on IN phase */
		if (setup_supported >= 0 && setup_supported <
					    UDC_EP0IN_MAX_PKT_SIZE) {
			pch_udc_ep_clear_nak(&(dev->ep[UDC_EP0IN_IDX]));
			/* Gadget would have queued a request when
			 * we called the setup */
			if (!(dev->setup_data.bRequestType & USB_DIR_IN)) {
				pch_udc_set_dma(dev, DMA_DIR_RX);
				pch_udc_ep_clear_nak(ep);
			}
		} else if (setup_supported < 0) {
			/* if unsupported request, then stall */
			pch_udc_ep_set_stall(&(dev->ep[UDC_EP0IN_IDX]));
			pch_udc_enable_ep_interrupts(ep->dev,
						PCH_UDC_EPINT(ep->in, ep->num));
			dev->stall = 0;
			pch_udc_set_dma(dev, DMA_DIR_RX);
		} else {
			dev->waiting_zlp_ack = 1;
		}
	} else if ((((stat & UDC_EPSTS_OUT_MASK) >> UDC_EPSTS_OUT_SHIFT) ==
		     UDC_EPSTS_OUT_DATA) && !dev->stall) {
		pch_udc_clear_dma(dev, DMA_DIR_RX);
		pch_udc_ep_set_ddptr(ep, 0);
		if (!list_empty(&ep->queue)) {
			ep->epsts = stat;
			pch_udc_svc_data_out(dev, PCH_UDC_EP0);
		}
		pch_udc_set_dma(dev, DMA_DIR_RX);
	}
	pch_udc_ep_set_rrdy(ep);
}


/**
 * pch_udc_postsvc_epinters() - This function enables end point interrupts
 *				and clears NAK status
 * @dev:	Reference to the device structure
 * @ep_num:	End point number
 */
static void pch_udc_postsvc_epinters(struct pch_udc_dev *dev, int ep_num)
{
	struct pch_udc_ep	*ep = &dev->ep[UDC_EPIN_IDX(ep_num)];
	if (list_empty(&ep->queue))
		return;
	pch_udc_enable_ep_interrupts(ep->dev, PCH_UDC_EPINT(ep->in, ep->num));
	pch_udc_ep_clear_nak(ep);
}

/**
 * pch_udc_read_all_epstatus() - This function read all endpoint status
 * @dev:	Reference to the device structure
 * @ep_intr:	Status of endpoint interrupt
 */
static void pch_udc_read_all_epstatus(struct pch_udc_dev *dev, u32 ep_intr)
{
	int i;
	struct pch_udc_ep	*ep;

	for (i = 0; i < PCH_UDC_USED_EP_NUM; i++) {
		/* IN */
		if (ep_intr & (0x1 << i)) {
			ep = &dev->ep[UDC_EPIN_IDX(i)];
			ep->epsts = pch_udc_read_ep_status(ep);
			pch_udc_clear_ep_status(ep, ep->epsts);
		}
		/* OUT */
		if (ep_intr & (0x10000 << i)) {
			ep = &dev->ep[UDC_EPOUT_IDX(i)];
			ep->epsts = pch_udc_read_ep_status(ep);
			pch_udc_clear_ep_status(ep, ep->epsts);
		}
	}
}

/**
 * pch_udc_activate_control_ep() - This function enables the control endpoints
 *					for traffic after a reset
 * @dev:	Reference to the device structure
 */
static void pch_udc_activate_control_ep(struct pch_udc_dev *dev)
{
	struct pch_udc_ep	*ep;
	u32 val;

	/* Setup the IN endpoint */
	ep = &dev->ep[UDC_EP0IN_IDX];
	pch_udc_clear_ep_control(ep);
	pch_udc_ep_fifo_flush(ep, ep->in);
	pch_udc_ep_set_bufsz(ep, UDC_EP0IN_BUFF_SIZE, ep->in);
	pch_udc_ep_set_maxpkt(ep, UDC_EP0IN_MAX_PKT_SIZE);
	/* Initialize the IN EP Descriptor */
	ep->td_data      = NULL;
	ep->td_stp       = NULL;
	ep->td_data_phys = 0;
	ep->td_stp_phys  = 0;

	/* Setup the OUT endpoint */
	ep = &dev->ep[UDC_EP0OUT_IDX];
	pch_udc_clear_ep_control(ep);
	pch_udc_ep_fifo_flush(ep, ep->in);
	pch_udc_ep_set_bufsz(ep, UDC_EP0OUT_BUFF_SIZE, ep->in);
	pch_udc_ep_set_maxpkt(ep, UDC_EP0OUT_MAX_PKT_SIZE);
	val = UDC_EP0OUT_MAX_PKT_SIZE << UDC_CSR_NE_MAX_PKT_SHIFT;
	pch_udc_write_csr(ep->dev, val, UDC_EP0OUT_IDX);

	/* Initialize the SETUP buffer */
	pch_udc_init_setup_buff(ep->td_stp);
	/* Write the pointer address of dma descriptor */
	pch_udc_ep_set_subptr(ep, ep->td_stp_phys);
	/* Write the pointer address of Setup descriptor */
	pch_udc_ep_set_ddptr(ep, ep->td_data_phys);

	/* Initialize the dma descriptor */
	ep->td_data->status  = PCH_UDC_DMA_LAST;
	ep->td_data->dataptr = dev->dma_addr;
	ep->td_data->next    = ep->td_data_phys;

	pch_udc_ep_clear_nak(ep);
}


/**
 * pch_udc_svc_ur_interrupt() - This function handles a USB reset interrupt
 * @dev:	Reference to driver structure
 */
static void pch_udc_svc_ur_interrupt(struct pch_udc_dev *dev)
{
	struct pch_udc_ep	*ep;
	int i;

	pch_udc_clear_dma(dev, DMA_DIR_TX);
	pch_udc_clear_dma(dev, DMA_DIR_RX);
	/* Mask all endpoint interrupts */
	pch_udc_disable_ep_interrupts(dev, UDC_EPINT_MSK_DISABLE_ALL);
	/* clear all endpoint interrupts */
	pch_udc_write_ep_interrupts(dev, UDC_EPINT_MSK_DISABLE_ALL);

	for (i = 0; i < PCH_UDC_EP_NUM; i++) {
		ep = &dev->ep[i];
		pch_udc_clear_ep_status(ep, UDC_EPSTS_ALL_CLR_MASK);
		pch_udc_clear_ep_control(ep);
		pch_udc_ep_set_ddptr(ep, 0);
		pch_udc_write_csr(ep->dev, 0x00, i);
	}
	dev->stall = 0;
	dev->prot_stall = 0;
	dev->waiting_zlp_ack = 0;
	dev->set_cfg_not_acked = 0;

	/* disable ep to empty req queue. Skip the control EP's */
	for (i = 0; i < (PCH_UDC_USED_EP_NUM*2); i++) {
		ep = &dev->ep[i];
		pch_udc_ep_set_nak(ep);
		pch_udc_ep_fifo_flush(ep, ep->in);
		/* Complete request queue */
		empty_req_queue(ep);
	}
	if (dev->driver) {
		spin_unlock(&dev->lock);
		usb_gadget_udc_reset(&dev->gadget, dev->driver);
		spin_lock(&dev->lock);
	}
}

/**
 * pch_udc_svc_enum_interrupt() - This function handles a USB speed enumeration
 *				done interrupt
 * @dev:	Reference to driver structure
 */
static void pch_udc_svc_enum_interrupt(struct pch_udc_dev *dev)
{
	u32 dev_stat, dev_speed;
	u32 speed = USB_SPEED_FULL;

	dev_stat = pch_udc_read_device_status(dev);
	dev_speed = (dev_stat & UDC_DEVSTS_ENUM_SPEED_MASK) >>
						 UDC_DEVSTS_ENUM_SPEED_SHIFT;
	switch (dev_speed) {
	case UDC_DEVSTS_ENUM_SPEED_HIGH:
		speed = USB_SPEED_HIGH;
		break;
	case  UDC_DEVSTS_ENUM_SPEED_FULL:
		speed = USB_SPEED_FULL;
		break;
	case  UDC_DEVSTS_ENUM_SPEED_LOW:
		speed = USB_SPEED_LOW;
		break;
	default:
		BUG();
	}
	dev->gadget.speed = speed;
	pch_udc_activate_control_ep(dev);
	pch_udc_enable_ep_interrupts(dev, UDC_EPINT_IN_EP0 | UDC_EPINT_OUT_EP0);
	pch_udc_set_dma(dev, DMA_DIR_TX);
	pch_udc_set_dma(dev, DMA_DIR_RX);
	pch_udc_ep_set_rrdy(&(dev->ep[UDC_EP0OUT_IDX]));

	/* enable device interrupts */
	pch_udc_enable_interrupts(dev, UDC_DEVINT_UR | UDC_DEVINT_US |
					UDC_DEVINT_ES | UDC_DEVINT_ENUM |
					UDC_DEVINT_SI | UDC_DEVINT_SC);
}

/**
 * pch_udc_svc_intf_interrupt() - This function handles a set interface
 *				  interrupt
 * @dev:	Reference to driver structure
 */
static void pch_udc_svc_intf_interrupt(struct pch_udc_dev *dev)
{
	u32 reg, dev_stat = 0;
	int i;

	dev_stat = pch_udc_read_device_status(dev);
	dev->cfg_data.cur_intf = (dev_stat & UDC_DEVSTS_INTF_MASK) >>
							 UDC_DEVSTS_INTF_SHIFT;
	dev->cfg_data.cur_alt = (dev_stat & UDC_DEVSTS_ALT_MASK) >>
							 UDC_DEVSTS_ALT_SHIFT;
	dev->set_cfg_not_acked = 1;
	/* Construct the usb request for gadget driver and inform it */
	memset(&dev->setup_data, 0 , sizeof dev->setup_data);
	dev->setup_data.bRequest = USB_REQ_SET_INTERFACE;
	dev->setup_data.bRequestType = USB_RECIP_INTERFACE;
	dev->setup_data.wValue = cpu_to_le16(dev->cfg_data.cur_alt);
	dev->setup_data.wIndex = cpu_to_le16(dev->cfg_data.cur_intf);
	/* programm the Endpoint Cfg registers */
	/* Only one end point cfg register */
	reg = pch_udc_read_csr(dev, UDC_EP0OUT_IDX);
	reg = (reg & ~UDC_CSR_NE_INTF_MASK) |
	      (dev->cfg_data.cur_intf << UDC_CSR_NE_INTF_SHIFT);
	reg = (reg & ~UDC_CSR_NE_ALT_MASK) |
	      (dev->cfg_data.cur_alt << UDC_CSR_NE_ALT_SHIFT);
	pch_udc_write_csr(dev, reg, UDC_EP0OUT_IDX);
	for (i = 0; i < PCH_UDC_USED_EP_NUM * 2; i++) {
		/* clear stall bits */
		pch_udc_ep_clear_stall(&(dev->ep[i]));
		dev->ep[i].halted = 0;
	}
	dev->stall = 0;
	pch_udc_gadget_setup(dev);
}

/**
 * pch_udc_svc_cfg_interrupt() - This function handles a set configuration
 *				interrupt
 * @dev:	Reference to driver structure
 */
static void pch_udc_svc_cfg_interrupt(struct pch_udc_dev *dev)
{
	int i;
	u32 reg, dev_stat = 0;

	dev_stat = pch_udc_read_device_status(dev);
	dev->set_cfg_not_acked = 1;
	dev->cfg_data.cur_cfg = (dev_stat & UDC_DEVSTS_CFG_MASK) >>
				UDC_DEVSTS_CFG_SHIFT;
	/* make usb request for gadget driver */
	memset(&dev->setup_data, 0 , sizeof dev->setup_data);
	dev->setup_data.bRequest = USB_REQ_SET_CONFIGURATION;
	dev->setup_data.wValue = cpu_to_le16(dev->cfg_data.cur_cfg);
	/* program the NE registers */
	/* Only one end point cfg register */
	reg = pch_udc_read_csr(dev, UDC_EP0OUT_IDX);
	reg = (reg & ~UDC_CSR_NE_CFG_MASK) |
	      (dev->cfg_data.cur_cfg << UDC_CSR_NE_CFG_SHIFT);
	pch_udc_write_csr(dev, reg, UDC_EP0OUT_IDX);
	for (i = 0; i < PCH_UDC_USED_EP_NUM * 2; i++) {
		/* clear stall bits */
		pch_udc_ep_clear_stall(&(dev->ep[i]));
		dev->ep[i].halted = 0;
	}
	dev->stall = 0;

	/* call gadget zero with setup data received */
	pch_udc_gadget_setup(dev);
}

/**
 * pch_udc_dev_isr() - This function services device interrupts
 *			by invoking appropriate routines.
 * @dev:	Reference to the device structure
 * @dev_intr:	The Device interrupt status.
 */
static void pch_udc_dev_isr(struct pch_udc_dev *dev, u32 dev_intr)
{
	int vbus;

	/* USB Reset Interrupt */
	if (dev_intr & UDC_DEVINT_UR) {
		pch_udc_svc_ur_interrupt(dev);
		dev_dbg(&dev->pdev->dev, "USB_RESET\n");
	}
	/* Enumeration Done Interrupt */
	if (dev_intr & UDC_DEVINT_ENUM) {
		pch_udc_svc_enum_interrupt(dev);
		dev_dbg(&dev->pdev->dev, "USB_ENUM\n");
	}
	/* Set Interface Interrupt */
	if (dev_intr & UDC_DEVINT_SI)
		pch_udc_svc_intf_interrupt(dev);
	/* Set Config Interrupt */
	if (dev_intr & UDC_DEVINT_SC)
		pch_udc_svc_cfg_interrupt(dev);
	/* USB Suspend interrupt */
	if (dev_intr & UDC_DEVINT_US) {
		if (dev->driver
			&& dev->driver->suspend) {
			spin_unlock(&dev->lock);
			dev->driver->suspend(&dev->gadget);
			spin_lock(&dev->lock);
		}

		vbus = pch_vbus_gpio_get_value(dev);
		if ((dev->vbus_session == 0)
			&& (vbus != 1)) {
			if (dev->driver && dev->driver->disconnect) {
				spin_unlock(&dev->lock);
				dev->driver->disconnect(&dev->gadget);
				spin_lock(&dev->lock);
			}
			pch_udc_reconnect(dev);
		} else if ((dev->vbus_session == 0)
			&& (vbus == 1)
			&& !dev->vbus_gpio.intr)
			schedule_work(&dev->vbus_gpio.irq_work_fall);

		dev_dbg(&dev->pdev->dev, "USB_SUSPEND\n");
	}
	/* Clear the SOF interrupt, if enabled */
	if (dev_intr & UDC_DEVINT_SOF)
		dev_dbg(&dev->pdev->dev, "SOF\n");
	/* ES interrupt, IDLE > 3ms on the USB */
	if (dev_intr & UDC_DEVINT_ES)
		dev_dbg(&dev->pdev->dev, "ES\n");
	/* RWKP interrupt */
	if (dev_intr & UDC_DEVINT_RWKP)
		dev_dbg(&dev->pdev->dev, "RWKP\n");
}

/**
 * pch_udc_isr() - This function handles interrupts from the PCH USB Device
 * @irq:	Interrupt request number
 * @pdev:	Reference to the device structure
 */
static irqreturn_t pch_udc_isr(int irq, void *pdev)
{
	struct pch_udc_dev *dev = (struct pch_udc_dev *) pdev;
	u32 dev_intr, ep_intr;
	int i;

	dev_intr = pch_udc_read_device_interrupts(dev);
	ep_intr = pch_udc_read_ep_interrupts(dev);

	/* For a hot plug, this find that the controller is hung up. */
	if (dev_intr == ep_intr)
		if (dev_intr == pch_udc_readl(dev, UDC_DEVCFG_ADDR)) {
			dev_dbg(&dev->pdev->dev, "UDC: Hung up\n");
			/* The controller is reset */
			pch_udc_writel(dev, UDC_SRST, UDC_SRST_ADDR);
			return IRQ_HANDLED;
		}
	if (dev_intr)
		/* Clear device interrupts */
		pch_udc_write_device_interrupts(dev, dev_intr);
	if (ep_intr)
		/* Clear ep interrupts */
		pch_udc_write_ep_interrupts(dev, ep_intr);
	if (!dev_intr && !ep_intr)
		return IRQ_NONE;
	spin_lock(&dev->lock);
	if (dev_intr)
		pch_udc_dev_isr(dev, dev_intr);
	if (ep_intr) {
		pch_udc_read_all_epstatus(dev, ep_intr);
		/* Process Control In interrupts, if present */
		if (ep_intr & UDC_EPINT_IN_EP0) {
			pch_udc_svc_control_in(dev);
			pch_udc_postsvc_epinters(dev, 0);
		}
		/* Process Control Out interrupts, if present */
		if (ep_intr & UDC_EPINT_OUT_EP0)
			pch_udc_svc_control_out(dev);
		/* Process data in end point interrupts */
		for (i = 1; i < PCH_UDC_USED_EP_NUM; i++) {
			if (ep_intr & (1 <<  i)) {
				pch_udc_svc_data_in(dev, i);
				pch_udc_postsvc_epinters(dev, i);
			}
		}
		/* Process data out end point interrupts */
		for (i = UDC_EPINT_OUT_SHIFT + 1; i < (UDC_EPINT_OUT_SHIFT +
						 PCH_UDC_USED_EP_NUM); i++)
			if (ep_intr & (1 <<  i))
				pch_udc_svc_data_out(dev, i -
							 UDC_EPINT_OUT_SHIFT);
	}
	spin_unlock(&dev->lock);
	return IRQ_HANDLED;
}

/**
 * pch_udc_setup_ep0() - This function enables control endpoint for traffic
 * @dev:	Reference to the device structure
 */
static void pch_udc_setup_ep0(struct pch_udc_dev *dev)
{
	/* enable ep0 interrupts */
	pch_udc_enable_ep_interrupts(dev, UDC_EPINT_IN_EP0 |
						UDC_EPINT_OUT_EP0);
	/* enable device interrupts */
	pch_udc_enable_interrupts(dev, UDC_DEVINT_UR | UDC_DEVINT_US |
				       UDC_DEVINT_ES | UDC_DEVINT_ENUM |
				       UDC_DEVINT_SI | UDC_DEVINT_SC);
}

/**
 * pch_udc_pcd_reinit() - This API initializes the endpoint structures
 * @dev:	Reference to the driver structure
 */
static void pch_udc_pcd_reinit(struct pch_udc_dev *dev)
{
	const char *const ep_string[] = {
		ep0_string, "ep0out", "ep1in", "ep1out", "ep2in", "ep2out",
		"ep3in", "ep3out", "ep4in", "ep4out", "ep5in", "ep5out",
		"ep6in", "ep6out", "ep7in", "ep7out", "ep8in", "ep8out",
		"ep9in", "ep9out", "ep10in", "ep10out", "ep11in", "ep11out",
		"ep12in", "ep12out", "ep13in", "ep13out", "ep14in", "ep14out",
		"ep15in", "ep15out",
	};
	int i;

	dev->gadget.speed = USB_SPEED_UNKNOWN;
	INIT_LIST_HEAD(&dev->gadget.ep_list);

	/* Initialize the endpoints structures */
	memset(dev->ep, 0, sizeof dev->ep);
	for (i = 0; i < PCH_UDC_EP_NUM; i++) {
		struct pch_udc_ep *ep = &dev->ep[i];
		ep->dev = dev;
		ep->halted = 1;
		ep->num = i / 2;
		ep->in = ~i & 1;
		ep->ep.name = ep_string[i];
		ep->ep.ops = &pch_udc_ep_ops;
		if (ep->in) {
			ep->offset_addr = ep->num * UDC_EP_REG_SHIFT;
			ep->ep.caps.dir_in = true;
		} else {
			ep->offset_addr = (UDC_EPINT_OUT_SHIFT + ep->num) *
					  UDC_EP_REG_SHIFT;
			ep->ep.caps.dir_out = true;
		}
		if (i == UDC_EP0IN_IDX || i == UDC_EP0OUT_IDX) {
			ep->ep.caps.type_control = true;
		} else {
			ep->ep.caps.type_iso = true;
			ep->ep.caps.type_bulk = true;
			ep->ep.caps.type_int = true;
		}
		/* need to set ep->ep.maxpacket and set Default Configuration?*/
		usb_ep_set_maxpacket_limit(&ep->ep, UDC_BULK_MAX_PKT_SIZE);
		list_add_tail(&ep->ep.ep_list, &dev->gadget.ep_list);
		INIT_LIST_HEAD(&ep->queue);
	}
	usb_ep_set_maxpacket_limit(&dev->ep[UDC_EP0IN_IDX].ep, UDC_EP0IN_MAX_PKT_SIZE);
	usb_ep_set_maxpacket_limit(&dev->ep[UDC_EP0OUT_IDX].ep, UDC_EP0OUT_MAX_PKT_SIZE);

	/* remove ep0 in and out from the list.  They have own pointer */
	list_del_init(&dev->ep[UDC_EP0IN_IDX].ep.ep_list);
	list_del_init(&dev->ep[UDC_EP0OUT_IDX].ep.ep_list);

	dev->gadget.ep0 = &dev->ep[UDC_EP0IN_IDX].ep;
	INIT_LIST_HEAD(&dev->gadget.ep0->ep_list);
}

/**
 * pch_udc_pcd_init() - This API initializes the driver structure
 * @dev:	Reference to the driver structure
 *
 * Return codes:
 *	0:		Success
 *	-ERRNO:		All kind of errors when retrieving VBUS GPIO
 */
static int pch_udc_pcd_init(struct pch_udc_dev *dev)
{
	int ret;

	pch_udc_init(dev);
	pch_udc_pcd_reinit(dev);

	ret = pch_vbus_gpio_init(dev);
	if (ret)
		pch_udc_exit(dev);
	return ret;
}

/**
 * init_dma_pools() - create dma pools during initialization
 * @dev:	reference to struct pci_dev
 */
static int init_dma_pools(struct pch_udc_dev *dev)
{
	struct pch_udc_stp_dma_desc	*td_stp;
	struct pch_udc_data_dma_desc	*td_data;
	void				*ep0out_buf;

	/* DMA setup */
	dev->data_requests = dma_pool_create("data_requests", &dev->pdev->dev,
		sizeof(struct pch_udc_data_dma_desc), 0, 0);
	if (!dev->data_requests) {
		dev_err(&dev->pdev->dev, "%s: can't get request data pool\n",
			__func__);
		return -ENOMEM;
	}

	/* dma desc for setup data */
	dev->stp_requests = dma_pool_create("setup requests", &dev->pdev->dev,
		sizeof(struct pch_udc_stp_dma_desc), 0, 0);
	if (!dev->stp_requests) {
		dev_err(&dev->pdev->dev, "%s: can't get setup request pool\n",
			__func__);
		return -ENOMEM;
	}
	/* setup */
	td_stp = dma_pool_alloc(dev->stp_requests, GFP_KERNEL,
				&dev->ep[UDC_EP0OUT_IDX].td_stp_phys);
	if (!td_stp) {
		dev_err(&dev->pdev->dev,
			"%s: can't allocate setup dma descriptor\n", __func__);
		return -ENOMEM;
	}
	dev->ep[UDC_EP0OUT_IDX].td_stp = td_stp;

	/* data: 0 packets !? */
	td_data = dma_pool_alloc(dev->data_requests, GFP_KERNEL,
				&dev->ep[UDC_EP0OUT_IDX].td_data_phys);
	if (!td_data) {
		dev_err(&dev->pdev->dev,
			"%s: can't allocate data dma descriptor\n", __func__);
		return -ENOMEM;
	}
	dev->ep[UDC_EP0OUT_IDX].td_data = td_data;
	dev->ep[UDC_EP0IN_IDX].td_stp = NULL;
	dev->ep[UDC_EP0IN_IDX].td_stp_phys = 0;
	dev->ep[UDC_EP0IN_IDX].td_data = NULL;
	dev->ep[UDC_EP0IN_IDX].td_data_phys = 0;

	ep0out_buf = devm_kzalloc(&dev->pdev->dev, UDC_EP0OUT_BUFF_SIZE * 4,
				  GFP_KERNEL);
	if (!ep0out_buf)
		return -ENOMEM;
	dev->dma_addr = dma_map_single(&dev->pdev->dev, ep0out_buf,
				       UDC_EP0OUT_BUFF_SIZE * 4,
				       DMA_FROM_DEVICE);
	return dma_mapping_error(&dev->pdev->dev, dev->dma_addr);
}

static int pch_udc_start(struct usb_gadget *g,
		struct usb_gadget_driver *driver)
{
	struct pch_udc_dev	*dev = to_pch_udc(g);

	dev->driver = driver;

	/* get ready for ep0 traffic */
	pch_udc_setup_ep0(dev);

	/* clear SD */
	if ((pch_vbus_gpio_get_value(dev) != 0) || !dev->vbus_gpio.intr)
		pch_udc_clear_disconnect(dev);

	dev->connected = 1;
	return 0;
}

static int pch_udc_stop(struct usb_gadget *g)
{
	struct pch_udc_dev	*dev = to_pch_udc(g);

	pch_udc_disable_interrupts(dev, UDC_DEVINT_MSK);

	/* Assures that there are no pending requests with this driver */
	dev->driver = NULL;
	dev->connected = 0;

	/* set SD */
	pch_udc_set_disconnect(dev);

	return 0;
}

static void pch_vbus_gpio_remove_table(void *table)
{
	gpiod_remove_lookup_table(table);
}

static int pch_vbus_gpio_add_table(struct device *d, void *table)
{
	gpiod_add_lookup_table(table);
	return devm_add_action_or_reset(d, pch_vbus_gpio_remove_table, table);
}

static struct gpiod_lookup_table pch_udc_minnow_vbus_gpio_table = {
	.dev_id		= "0000:02:02.4",
	.table		= {
		GPIO_LOOKUP("sch_gpio.33158", 12, NULL, GPIO_ACTIVE_HIGH),
		{}
	},
};

static int pch_udc_minnow_platform_init(struct device *d)
{
	return pch_vbus_gpio_add_table(d, &pch_udc_minnow_vbus_gpio_table);
}

static int pch_udc_quark_platform_init(struct device *d)
{
	struct pch_udc_dev *dev = dev_get_drvdata(d);

	dev->bar = PCH_UDC_PCI_BAR_QUARK_X1000;
	return 0;
}

static void pch_udc_shutdown(struct pci_dev *pdev)
{
	struct pch_udc_dev *dev = pci_get_drvdata(pdev);

	pch_udc_disable_interrupts(dev, UDC_DEVINT_MSK);
	pch_udc_disable_ep_interrupts(dev, UDC_EPINT_MSK_DISABLE_ALL);

	/* disable the pullup so the host will think we're gone */
	pch_udc_set_disconnect(dev);
}

static void pch_udc_remove(struct pci_dev *pdev)
{
	struct pch_udc_dev	*dev = pci_get_drvdata(pdev);

	usb_del_gadget_udc(&dev->gadget);

	/* gadget driver must not be registered */
	if (dev->driver)
		dev_err(&pdev->dev,
			"%s: gadget driver still bound!!!\n", __func__);
	/* dma pool cleanup */
	dma_pool_destroy(dev->data_requests);

	if (dev->stp_requests) {
		/* cleanup DMA desc's for ep0in */
		if (dev->ep[UDC_EP0OUT_IDX].td_stp) {
			dma_pool_free(dev->stp_requests,
				dev->ep[UDC_EP0OUT_IDX].td_stp,
				dev->ep[UDC_EP0OUT_IDX].td_stp_phys);
		}
		if (dev->ep[UDC_EP0OUT_IDX].td_data) {
			dma_pool_free(dev->stp_requests,
				dev->ep[UDC_EP0OUT_IDX].td_data,
				dev->ep[UDC_EP0OUT_IDX].td_data_phys);
		}
		dma_pool_destroy(dev->stp_requests);
	}

	if (dev->dma_addr)
		dma_unmap_single(&dev->pdev->dev, dev->dma_addr,
				 UDC_EP0OUT_BUFF_SIZE * 4, DMA_FROM_DEVICE);

	pch_vbus_gpio_free(dev);

	pch_udc_exit(dev);
}

static int __maybe_unused pch_udc_suspend(struct device *d)
{
	struct pch_udc_dev *dev = dev_get_drvdata(d);

	pch_udc_disable_interrupts(dev, UDC_DEVINT_MSK);
	pch_udc_disable_ep_interrupts(dev, UDC_EPINT_MSK_DISABLE_ALL);

	return 0;
}

static int __maybe_unused pch_udc_resume(struct device *d)
{
	return 0;
}

static SIMPLE_DEV_PM_OPS(pch_udc_pm, pch_udc_suspend, pch_udc_resume);

typedef int (*platform_init_fn)(struct device *);

static int pch_udc_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	platform_init_fn platform_init = (platform_init_fn)id->driver_data;
	int			retval;
	struct pch_udc_dev	*dev;

	/* init */
	dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	/* pci setup */
	retval = pcim_enable_device(pdev);
	if (retval)
		return retval;

	dev->bar = PCH_UDC_PCI_BAR;
	dev->pdev = pdev;
	pci_set_drvdata(pdev, dev);

	/* Platform specific hook */
	if (platform_init) {
		retval = platform_init(&pdev->dev);
		if (retval)
			return retval;
	}

	/* PCI resource allocation */
	retval = pcim_iomap_regions(pdev, BIT(dev->bar), pci_name(pdev));
	if (retval)
		return retval;

	dev->base_addr = pcim_iomap_table(pdev)[dev->bar];

	/* initialize the hardware */
	retval = pch_udc_pcd_init(dev);
	if (retval)
		return retval;

	pci_enable_msi(pdev);

	retval = devm_request_irq(&pdev->dev, pdev->irq, pch_udc_isr,
				  IRQF_SHARED, KBUILD_MODNAME, dev);
	if (retval) {
		dev_err(&pdev->dev, "%s: request_irq(%d) fail\n", __func__,
			pdev->irq);
		goto finished;
	}

	pci_set_master(pdev);
	pci_try_set_mwi(pdev);

	/* device struct setup */
	spin_lock_init(&dev->lock);
	dev->gadget.ops = &pch_udc_ops;

	retval = init_dma_pools(dev);
	if (retval)
		goto finished;

	dev->gadget.name = KBUILD_MODNAME;
	dev->gadget.max_speed = USB_SPEED_HIGH;

	/* Put the device in disconnected state till a driver is bound */
	pch_udc_set_disconnect(dev);
	retval = usb_add_gadget_udc(&pdev->dev, &dev->gadget);
	if (retval)
		goto finished;
	return 0;

finished:
	pch_udc_remove(pdev);
	return retval;
}

static const struct pci_device_id pch_udc_pcidev_id[] = {
	{
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_QUARK_X1000_UDC),
		.class = PCI_CLASS_SERIAL_USB_DEVICE,
		.class_mask = 0xffffffff,
		.driver_data = (kernel_ulong_t)&pch_udc_quark_platform_init,
	},
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_EG20T_UDC,
			       PCI_VENDOR_ID_CIRCUITCO, PCI_SUBSYSTEM_ID_CIRCUITCO_MINNOWBOARD),
		.class = PCI_CLASS_SERIAL_USB_DEVICE,
		.class_mask = 0xffffffff,
		.driver_data = (kernel_ulong_t)&pch_udc_minnow_platform_init,
	},
	{
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_EG20T_UDC),
		.class = PCI_CLASS_SERIAL_USB_DEVICE,
		.class_mask = 0xffffffff,
	},
	{
		PCI_DEVICE(PCI_VENDOR_ID_ROHM, PCI_DEVICE_ID_ML7213_IOH_UDC),
		.class = PCI_CLASS_SERIAL_USB_DEVICE,
		.class_mask = 0xffffffff,
	},
	{
		PCI_DEVICE(PCI_VENDOR_ID_ROHM, PCI_DEVICE_ID_ML7831_IOH_UDC),
		.class = PCI_CLASS_SERIAL_USB_DEVICE,
		.class_mask = 0xffffffff,
	},
	{ 0 },
};

MODULE_DEVICE_TABLE(pci, pch_udc_pcidev_id);

static struct pci_driver pch_udc_driver = {
	.name =	KBUILD_MODNAME,
	.id_table =	pch_udc_pcidev_id,
	.probe =	pch_udc_probe,
	.remove =	pch_udc_remove,
	.shutdown =	pch_udc_shutdown,
	.driver = {
		.pm = &pch_udc_pm,
	},
};

module_pci_driver(pch_udc_driver);

MODULE_DESCRIPTION("Intel EG20T USB Device Controller");
MODULE_AUTHOR("LAPIS Semiconductor, <tomoya-linux@dsn.lapis-semi.com>");
MODULE_LICENSE("GPL");
