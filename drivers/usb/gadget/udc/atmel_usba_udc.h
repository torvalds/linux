/*
 * Driver for the Atmel USBA high speed USB device controller
 *
 * Copyright (C) 2005-2007 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __LINUX_USB_GADGET_USBA_UDC_H__
#define __LINUX_USB_GADGET_USBA_UDC_H__

/* USB register offsets */
#define USBA_CTRL				0x0000
#define USBA_FNUM				0x0004
#define USBA_INT_ENB				0x0010
#define USBA_INT_STA				0x0014
#define USBA_INT_CLR				0x0018
#define USBA_EPT_RST				0x001c
#define USBA_TST				0x00e0

/* USB endpoint register offsets */
#define USBA_EPT_CFG				0x0000
#define USBA_EPT_CTL_ENB			0x0004
#define USBA_EPT_CTL_DIS			0x0008
#define USBA_EPT_CTL				0x000c
#define USBA_EPT_SET_STA			0x0014
#define USBA_EPT_CLR_STA			0x0018
#define USBA_EPT_STA				0x001c

/* USB DMA register offsets */
#define USBA_DMA_NXT_DSC			0x0000
#define USBA_DMA_ADDRESS			0x0004
#define USBA_DMA_CONTROL			0x0008
#define USBA_DMA_STATUS				0x000c

/* Bitfields in CTRL */
#define USBA_DEV_ADDR_OFFSET			0
#define USBA_DEV_ADDR_SIZE			7
#define USBA_FADDR_EN				(1 <<  7)
#define USBA_EN_USBA				(1 <<  8)
#define USBA_DETACH				(1 <<  9)
#define USBA_REMOTE_WAKE_UP			(1 << 10)
#define USBA_PULLD_DIS				(1 << 11)

#if defined(CONFIG_AVR32)
#define USBA_ENABLE_MASK			USBA_EN_USBA
#define USBA_DISABLE_MASK			0
#elif defined(CONFIG_ARCH_AT91)
#define USBA_ENABLE_MASK			(USBA_EN_USBA | USBA_PULLD_DIS)
#define USBA_DISABLE_MASK			USBA_DETACH
#endif /* CONFIG_ARCH_AT91 */

/* Bitfields in FNUM */
#define USBA_MICRO_FRAME_NUM_OFFSET		0
#define USBA_MICRO_FRAME_NUM_SIZE		3
#define USBA_FRAME_NUMBER_OFFSET		3
#define USBA_FRAME_NUMBER_SIZE			11
#define USBA_FRAME_NUM_ERROR			(1 << 31)

/* Bitfields in INT_ENB/INT_STA/INT_CLR */
#define USBA_HIGH_SPEED				(1 <<  0)
#define USBA_DET_SUSPEND			(1 <<  1)
#define USBA_MICRO_SOF				(1 <<  2)
#define USBA_SOF				(1 <<  3)
#define USBA_END_OF_RESET			(1 <<  4)
#define USBA_WAKE_UP				(1 <<  5)
#define USBA_END_OF_RESUME			(1 <<  6)
#define USBA_UPSTREAM_RESUME			(1 <<  7)
#define USBA_EPT_INT_OFFSET			8
#define USBA_EPT_INT_SIZE			16
#define USBA_DMA_INT_OFFSET			24
#define USBA_DMA_INT_SIZE			8

/* Bitfields in EPT_RST */
#define USBA_RST_OFFSET				0
#define USBA_RST_SIZE				16

/* Bitfields in USBA_TST */
#define USBA_SPEED_CFG_OFFSET			0
#define USBA_SPEED_CFG_SIZE			2
#define USBA_TST_J_MODE				(1 <<  2)
#define USBA_TST_K_MODE				(1 <<  3)
#define USBA_TST_PKT_MODE			(1 <<  4)
#define USBA_OPMODE2				(1 <<  5)

/* Bitfields in EPT_CFG */
#define USBA_EPT_SIZE_OFFSET			0
#define USBA_EPT_SIZE_SIZE			3
#define USBA_EPT_DIR_IN				(1 <<  3)
#define USBA_EPT_TYPE_OFFSET			4
#define USBA_EPT_TYPE_SIZE			2
#define USBA_BK_NUMBER_OFFSET			6
#define USBA_BK_NUMBER_SIZE			2
#define USBA_NB_TRANS_OFFSET			8
#define USBA_NB_TRANS_SIZE			2
#define USBA_EPT_MAPPED				(1 << 31)

/* Bitfields in EPT_CTL/EPT_CTL_ENB/EPT_CTL_DIS */
#define USBA_EPT_ENABLE				(1 <<  0)
#define USBA_AUTO_VALID				(1 <<  1)
#define USBA_INTDIS_DMA				(1 <<  3)
#define USBA_NYET_DIS				(1 <<  4)
#define USBA_DATAX_RX				(1 <<  6)
#define USBA_MDATA_RX				(1 <<  7)
/* Bits 8-15 and 31 enable interrupts for respective bits in EPT_STA */
#define USBA_BUSY_BANK_IE			(1 << 18)

/* Bitfields in EPT_SET_STA/EPT_CLR_STA/EPT_STA */
#define USBA_FORCE_STALL			(1 <<  5)
#define USBA_TOGGLE_CLR				(1 <<  6)
#define USBA_TOGGLE_SEQ_OFFSET			6
#define USBA_TOGGLE_SEQ_SIZE			2
#define USBA_ERR_OVFLW				(1 <<  8)
#define USBA_RX_BK_RDY				(1 <<  9)
#define USBA_KILL_BANK				(1 <<  9)
#define USBA_TX_COMPLETE			(1 << 10)
#define USBA_TX_PK_RDY				(1 << 11)
#define USBA_ISO_ERR_TRANS			(1 << 11)
#define USBA_RX_SETUP				(1 << 12)
#define USBA_ISO_ERR_FLOW			(1 << 12)
#define USBA_STALL_SENT				(1 << 13)
#define USBA_ISO_ERR_CRC			(1 << 13)
#define USBA_ISO_ERR_NBTRANS			(1 << 13)
#define USBA_NAK_IN				(1 << 14)
#define USBA_ISO_ERR_FLUSH			(1 << 14)
#define USBA_NAK_OUT				(1 << 15)
#define USBA_CURRENT_BANK_OFFSET		16
#define USBA_CURRENT_BANK_SIZE			2
#define USBA_BUSY_BANKS_OFFSET			18
#define USBA_BUSY_BANKS_SIZE			2
#define USBA_BYTE_COUNT_OFFSET			20
#define USBA_BYTE_COUNT_SIZE			11
#define USBA_SHORT_PACKET			(1 << 31)

/* Bitfields in DMA_CONTROL */
#define USBA_DMA_CH_EN				(1 <<  0)
#define USBA_DMA_LINK				(1 <<  1)
#define USBA_DMA_END_TR_EN			(1 <<  2)
#define USBA_DMA_END_BUF_EN			(1 <<  3)
#define USBA_DMA_END_TR_IE			(1 <<  4)
#define USBA_DMA_END_BUF_IE			(1 <<  5)
#define USBA_DMA_DESC_LOAD_IE			(1 <<  6)
#define USBA_DMA_BURST_LOCK			(1 <<  7)
#define USBA_DMA_BUF_LEN_OFFSET			16
#define USBA_DMA_BUF_LEN_SIZE			16

/* Bitfields in DMA_STATUS */
#define USBA_DMA_CH_ACTIVE			(1 <<  1)
#define USBA_DMA_END_TR_ST			(1 <<  4)
#define USBA_DMA_END_BUF_ST			(1 <<  5)
#define USBA_DMA_DESC_LOAD_ST			(1 <<  6)

/* Constants for SPEED_CFG */
#define USBA_SPEED_CFG_NORMAL			0
#define USBA_SPEED_CFG_FORCE_HIGH		2
#define USBA_SPEED_CFG_FORCE_FULL		3

/* Constants for EPT_SIZE */
#define USBA_EPT_SIZE_8				0
#define USBA_EPT_SIZE_16			1
#define USBA_EPT_SIZE_32			2
#define USBA_EPT_SIZE_64			3
#define USBA_EPT_SIZE_128			4
#define USBA_EPT_SIZE_256			5
#define USBA_EPT_SIZE_512			6
#define USBA_EPT_SIZE_1024			7

/* Constants for EPT_TYPE */
#define USBA_EPT_TYPE_CONTROL			0
#define USBA_EPT_TYPE_ISO			1
#define USBA_EPT_TYPE_BULK			2
#define USBA_EPT_TYPE_INT			3

/* Constants for BK_NUMBER */
#define USBA_BK_NUMBER_ZERO			0
#define USBA_BK_NUMBER_ONE			1
#define USBA_BK_NUMBER_DOUBLE			2
#define USBA_BK_NUMBER_TRIPLE			3

/* Bit manipulation macros */
#define USBA_BF(name, value)					\
	(((value) & ((1 << USBA_##name##_SIZE) - 1))		\
	 << USBA_##name##_OFFSET)
#define USBA_BFEXT(name, value)					\
	(((value) >> USBA_##name##_OFFSET)			\
	 & ((1 << USBA_##name##_SIZE) - 1))
#define USBA_BFINS(name, value, old)				\
	(((old) & ~(((1 << USBA_##name##_SIZE) - 1)		\
		    << USBA_##name##_OFFSET))			\
	 | USBA_BF(name, value))

/* Register access macros */
#ifdef CONFIG_AVR32
#define usba_io_readl	__raw_readl
#define usba_io_writel	__raw_writel
#define usba_io_writew	__raw_writew
#else
#define usba_io_readl	readl_relaxed
#define usba_io_writel	writel_relaxed
#define usba_io_writew	writew_relaxed
#endif

#define usba_readl(udc, reg)					\
	usba_io_readl((udc)->regs + USBA_##reg)
#define usba_writel(udc, reg, value)				\
	usba_io_writel((value), (udc)->regs + USBA_##reg)
#define usba_ep_readl(ep, reg)					\
	usba_io_readl((ep)->ep_regs + USBA_EPT_##reg)
#define usba_ep_writel(ep, reg, value)				\
	usba_io_writel((value), (ep)->ep_regs + USBA_EPT_##reg)
#define usba_dma_readl(ep, reg)					\
	usba_io_readl((ep)->dma_regs + USBA_DMA_##reg)
#define usba_dma_writel(ep, reg, value)				\
	usba_io_writel((value), (ep)->dma_regs + USBA_DMA_##reg)

/* Calculate base address for a given endpoint or DMA controller */
#define USBA_EPT_BASE(x)	(0x100 + (x) * 0x20)
#define USBA_DMA_BASE(x)	(0x300 + (x) * 0x10)
#define USBA_FIFO_BASE(x)	((x) << 16)

/* Synth parameters */
#define USBA_NR_DMAS		7

#define EP0_FIFO_SIZE		64
#define EP0_EPT_SIZE		USBA_EPT_SIZE_64
#define EP0_NR_BANKS		1

#define FIFO_IOMEM_ID	0
#define CTRL_IOMEM_ID	1

#define DBG_ERR		0x0001	/* report all error returns */
#define DBG_HW		0x0002	/* debug hardware initialization */
#define DBG_GADGET	0x0004	/* calls to/from gadget driver */
#define DBG_INT		0x0008	/* interrupts */
#define DBG_BUS		0x0010	/* report changes in bus state */
#define DBG_QUEUE	0x0020  /* debug request queue processing */
#define DBG_FIFO	0x0040  /* debug FIFO contents */
#define DBG_DMA		0x0080  /* debug DMA handling */
#define DBG_REQ		0x0100	/* print out queued request length */
#define DBG_ALL		0xffff
#define DBG_NONE	0x0000

#define DEBUG_LEVEL	(DBG_ERR)

#define DBG(level, fmt, ...)					\
	do {							\
		if ((level) & DEBUG_LEVEL)			\
			pr_debug("udc: " fmt, ## __VA_ARGS__);	\
	} while (0)

enum usba_ctrl_state {
	WAIT_FOR_SETUP,
	DATA_STAGE_IN,
	DATA_STAGE_OUT,
	STATUS_STAGE_IN,
	STATUS_STAGE_OUT,
	STATUS_STAGE_ADDR,
	STATUS_STAGE_TEST,
};
/*
  EP_STATE_IDLE,
  EP_STATE_SETUP,
  EP_STATE_IN_DATA,
  EP_STATE_OUT_DATA,
  EP_STATE_SET_ADDR_STATUS,
  EP_STATE_RX_STATUS,
  EP_STATE_TX_STATUS,
  EP_STATE_HALT,
*/

struct usba_dma_desc {
	dma_addr_t next;
	dma_addr_t addr;
	u32 ctrl;
};

struct usba_ep {
	int					state;
	void __iomem				*ep_regs;
	void __iomem				*dma_regs;
	void __iomem				*fifo;
	struct usb_ep				ep;
	struct usba_udc				*udc;

	struct list_head			queue;

	u16					fifo_size;
	u8					nr_banks;
	u8					index;
	unsigned int				can_dma:1;
	unsigned int				can_isoc:1;
	unsigned int				is_isoc:1;
	unsigned int				is_in:1;

#ifdef CONFIG_USB_GADGET_DEBUG_FS
	u32					last_dma_status;
	struct dentry				*debugfs_dir;
	struct dentry				*debugfs_queue;
	struct dentry				*debugfs_dma_status;
	struct dentry				*debugfs_state;
#endif
};

struct usba_request {
	struct usb_request			req;
	struct list_head			queue;

	u32					ctrl;

	unsigned int				submitted:1;
	unsigned int				last_transaction:1;
	unsigned int				using_dma:1;
	unsigned int				mapped:1;
};

struct usba_udc_errata {
	void (*toggle_bias)(struct usba_udc *udc, int is_on);
	void (*pulse_bias)(struct usba_udc *udc);
};

struct usba_udc {
	/* Protect hw registers from concurrent modifications */
	spinlock_t lock;

	/* Mutex to prevent concurrent start or stop */
	struct mutex vbus_mutex;

	void __iomem *regs;
	void __iomem *fifo;

	struct usb_gadget gadget;
	struct usb_gadget_driver *driver;
	struct platform_device *pdev;
	const struct usba_udc_errata *errata;
	int irq;
	int vbus_pin;
	int vbus_pin_inverted;
	int num_ep;
	struct clk *pclk;
	struct clk *hclk;
	struct usba_ep *usba_ep;
	bool bias_pulse_needed;
	bool clocked;

	u16 devstatus;

	u16 test_mode;
	int vbus_prev;

	u32 int_enb_cache;

#ifdef CONFIG_USB_GADGET_DEBUG_FS
	struct dentry *debugfs_root;
	struct dentry *debugfs_regs;
#endif
};

static inline struct usba_ep *to_usba_ep(struct usb_ep *ep)
{
	return container_of(ep, struct usba_ep, ep);
}

static inline struct usba_request *to_usba_req(struct usb_request *req)
{
	return container_of(req, struct usba_request, req);
}

static inline struct usba_udc *to_usba_udc(struct usb_gadget *gadget)
{
	return container_of(gadget, struct usba_udc, gadget);
}

#define ep_is_control(ep)	((ep)->index == 0)
#define ep_is_idle(ep)		((ep)->state == EP_STATE_IDLE)

#endif /* __LINUX_USB_GADGET_USBA_UDC_H */
