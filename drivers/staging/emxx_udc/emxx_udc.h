/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  EMXX FCD (Function Controller Driver) for USB.
 *
 *  Copyright (C) 2010 Renesas Electronics Corporation
 */

#ifndef _LINUX_EMXX_H
#define _LINUX_EMXX_H

/*---------------------------------------------------------------------------*/

/*----------------- Default define */
#define	USE_DMA	1
#define USE_SUSPEND_WAIT	1

/*------------ Board dependence(Resource) */
#define	VBUS_VALUE		GPIO_VBUS

/* below hacked up for staging integration */
#define GPIO_VBUS 0 /* GPIO_P153 on KZM9D */
#define INT_VBUS 0 /* IRQ for GPIO_P153 */
struct gpio_desc *vbus_gpio;
int vbus_irq;

/*------------ Board dependence(Wait) */

/* CHATTERING wait time ms */
#define VBUS_CHATTERING_MDELAY		1
/* DMA Abort wait time ms */
#define DMA_DISABLE_TIME		10

/*------------ Controller dependence */
#define NUM_ENDPOINTS		14		/* Endpoint */
#define REG_EP_NUM		15		/* Endpoint Register */
#define DMA_MAX_COUNT		256		/* DMA Block */

#define EPC_RST_DISABLE_TIME		1	/* 1 usec */
#define EPC_DIRPD_DISABLE_TIME		1	/* 1 msec */
#define EPC_PLL_LOCK_COUNT		1000	/* 1000 */
#define IN_DATA_EMPTY_COUNT		1000	/* 1000 */

#define CHATGER_TIME			700	/* 700msec */
#define USB_SUSPEND_TIME		2000	/* 2 sec */

/* U2F FLAG */
#define U2F_ENABLE		1
#define U2F_DISABLE		0

#define TEST_FORCE_ENABLE		(BIT(18) | BIT(16))

#define INT_SEL				BIT(10)
#define CONSTFS				BIT(9)
#define SOF_RCV				BIT(8)
#define RSUM_IN				BIT(7)
#define SUSPEND				BIT(6)
#define CONF				BIT(5)
#define DEFAULT				BIT(4)
#define CONNECTB			BIT(3)
#define PUE2				BIT(2)

#define MAX_TEST_MODE_NUM		0x05
#define TEST_MODE_SHIFT			16

/*------- (0x0004) USB Status Register */
#define SPEED_MODE			BIT(6)
#define HIGH_SPEED			BIT(6)

#define CONF				BIT(5)
#define DEFAULT				BIT(4)
#define USB_RST				BIT(3)
#define SPND_OUT			BIT(2)
#define RSUM_OUT			BIT(1)

/*------- (0x0008) USB Address Register */
#define USB_ADDR			0x007F0000
#define SOF_STATUS			BIT(15)
#define UFRAME				(BIT(14) | BIT(13) | BIT(12))
#define FRAME				0x000007FF

#define USB_ADRS_SHIFT			16

/*------- (0x000C) UTMI Characteristic 1 Register */
#define SQUSET				(BIT(7) | BIT(6) | BIT(5) | BIT(4))

#define USB_SQUSET			(BIT(6) | BIT(5) | BIT(4))

/*------- (0x0010) TEST Control Register */
#define FORCEHS				BIT(2)
#define CS_TESTMODEEN			BIT(1)
#define LOOPBACK			BIT(0)

/*------- (0x0018) Setup Data 0 Register */
/*------- (0x001C) Setup Data 1 Register */

/*------- (0x0020) USB Interrupt Status Register */
#define EPN_INT				0x00FFFF00
#define EP15_INT			BIT(23)
#define EP14_INT			BIT(22)
#define EP13_INT			BIT(21)
#define EP12_INT			BIT(20)
#define EP11_INT			BIT(19)
#define EP10_INT			BIT(18)
#define EP9_INT				BIT(17)
#define EP8_INT				BIT(16)
#define EP7_INT				BIT(15)
#define EP6_INT				BIT(14)
#define EP5_INT				BIT(13)
#define EP4_INT				BIT(12)
#define EP3_INT				BIT(11)
#define EP2_INT				BIT(10)
#define EP1_INT				BIT(9)
#define EP0_INT				BIT(8)
#define SPEED_MODE_INT			BIT(6)
#define SOF_ERROR_INT			BIT(5)
#define SOF_INT				BIT(4)
#define USB_RST_INT			BIT(3)
#define SPND_INT			BIT(2)
#define RSUM_INT			BIT(1)

#define USB_INT_STA_RW			0x7E

/*------- (0x0024) USB Interrupt Enable Register */
#define EP15_0_EN			0x00FFFF00
#define EP15_EN				BIT(23)
#define EP14_EN				BIT(22)
#define EP13_EN				BIT(21)
#define EP12_EN				BIT(20)
#define EP11_EN				BIT(19)
#define EP10_EN				BIT(18)
#define EP9_EN				BIT(17)
#define EP8_EN				BIT(16)
#define EP7_EN				BIT(15)
#define EP6_EN				BIT(14)
#define EP5_EN				BIT(13)
#define EP4_EN				BIT(12)
#define EP3_EN				BIT(11)
#define EP2_EN				BIT(10)
#define EP1_EN				BIT(9)
#define EP0_EN				BIT(8)
#define SPEED_MODE_EN			BIT(6)
#define SOF_ERROR_EN			BIT(5)
#define SOF_EN				BIT(4)
#define USB_RST_EN			BIT(3)
#define SPND_EN				BIT(2)
#define RSUM_EN				BIT(1)

#define USB_INT_EN_BIT	\
	(EP0_EN | SPEED_MODE_EN | USB_RST_EN | SPND_EN | RSUM_EN)

/*------- (0x0028) EP0 Control Register */
#define EP0_STGSEL			BIT(18)
#define EP0_OVERSEL			BIT(17)
#define EP0_AUTO			BIT(16)
#define EP0_PIDCLR			BIT(9)
#define EP0_BCLR			BIT(8)
#define EP0_DEND			BIT(7)
#define EP0_DW				(BIT(6) | BIT(5))
#define EP0_DW4				0
#define EP0_DW3				(BIT(6) | BIT(5))
#define EP0_DW2				BIT(6)
#define EP0_DW1				BIT(5)

#define EP0_INAK_EN			BIT(4)
#define EP0_PERR_NAK_CLR		BIT(3)
#define EP0_STL				BIT(2)
#define EP0_INAK			BIT(1)
#define EP0_ONAK			BIT(0)

/*------- (0x002C) EP0 Status Register */
#define EP0_PID				BIT(18)
#define EP0_PERR_NAK			BIT(17)
#define EP0_PERR_NAK_INT		BIT(16)
#define EP0_OUT_NAK_INT			BIT(15)
#define EP0_OUT_NULL			BIT(14)
#define EP0_OUT_FULL			BIT(13)
#define EP0_OUT_EMPTY			BIT(12)
#define EP0_IN_NAK_INT			BIT(11)
#define EP0_IN_DATA			BIT(10)
#define EP0_IN_FULL			BIT(9)
#define EP0_IN_EMPTY			BIT(8)
#define EP0_OUT_NULL_INT		BIT(7)
#define EP0_OUT_OR_INT			BIT(6)
#define EP0_OUT_INT			BIT(5)
#define EP0_IN_INT			BIT(4)
#define EP0_STALL_INT			BIT(3)
#define STG_END_INT			BIT(2)
#define STG_START_INT			BIT(1)
#define SETUP_INT			BIT(0)

#define EP0_STATUS_RW_BIT	(BIT(16) | BIT(15) | BIT(11) | 0xFF)

/*------- (0x0030) EP0 Interrupt Enable Register */
#define EP0_PERR_NAK_EN			BIT(16)
#define EP0_OUT_NAK_EN			BIT(15)

#define EP0_IN_NAK_EN			BIT(11)

#define EP0_OUT_NULL_EN			BIT(7)
#define EP0_OUT_OR_EN			BIT(6)
#define EP0_OUT_EN			BIT(5)
#define EP0_IN_EN			BIT(4)
#define EP0_STALL_EN			BIT(3)
#define STG_END_EN			BIT(2)
#define STG_START_EN			BIT(1)
#define SETUP_EN			BIT(0)

#define EP0_INT_EN_BIT	\
	(EP0_OUT_OR_EN | EP0_OUT_EN | EP0_IN_EN | STG_END_EN | SETUP_EN)

/*------- (0x0034) EP0 Length Register */
#define EP0_LDATA			0x0000007F

/*------- (0x0038) EP0 Read Register */
/*------- (0x003C) EP0 Write Register */

/*------- (0x0040:) EPN Control Register */
#define EPN_EN				BIT(31)
#define EPN_BUF_TYPE			BIT(30)
#define EPN_BUF_SINGLE			BIT(30)

#define EPN_DIR0			BIT(26)
#define EPN_MODE			(BIT(25) | BIT(24))
#define EPN_BULK			0
#define EPN_INTERRUPT			BIT(24)
#define EPN_ISO				BIT(25)

#define EPN_OVERSEL			BIT(17)
#define EPN_AUTO			BIT(16)

#define EPN_IPIDCLR			BIT(11)
#define EPN_OPIDCLR			BIT(10)
#define EPN_BCLR			BIT(9)
#define EPN_CBCLR			BIT(8)
#define EPN_DEND			BIT(7)
#define EPN_DW				(BIT(6) | BIT(5))
#define EPN_DW4				0
#define EPN_DW3				(BIT(6) | BIT(5))
#define EPN_DW2				BIT(6)
#define EPN_DW1				BIT(5)

#define EPN_OSTL_EN			BIT(4)
#define EPN_ISTL			BIT(3)
#define EPN_OSTL			BIT(2)

#define EPN_ONAK			BIT(0)

/*------- (0x0044:) EPN Status Register	*/
#define EPN_ISO_PIDERR			BIT(29)		/* R */
#define EPN_OPID			BIT(28)		/* R */
#define EPN_OUT_NOTKN			BIT(27)		/* R */
#define EPN_ISO_OR			BIT(26)		/* R */

#define EPN_ISO_CRC			BIT(24)		/* R */
#define EPN_OUT_END_INT			BIT(23)		/* RW */
#define EPN_OUT_OR_INT			BIT(22)		/* RW */
#define EPN_OUT_NAK_ERR_INT		BIT(21)		/* RW */
#define EPN_OUT_STALL_INT		BIT(20)		/* RW */
#define EPN_OUT_INT			BIT(19)		/* RW */
#define EPN_OUT_NULL_INT		BIT(18)		/* RW */
#define EPN_OUT_FULL			BIT(17)		/* R */
#define EPN_OUT_EMPTY			BIT(16)		/* R */

#define EPN_IPID			BIT(10)		/* R */
#define EPN_IN_NOTKN			BIT(9)		/* R */
#define EPN_ISO_UR			BIT(8)		/* R */
#define EPN_IN_END_INT			BIT(7)		/* RW */

#define EPN_IN_NAK_ERR_INT		BIT(5)		/* RW */
#define EPN_IN_STALL_INT		BIT(4)		/* RW */
#define EPN_IN_INT			BIT(3)		/* RW */
#define EPN_IN_DATA			BIT(2)		/* R */
#define EPN_IN_FULL			BIT(1)		/* R */
#define EPN_IN_EMPTY			BIT(0)		/* R */

#define EPN_INT_EN	\
	(EPN_OUT_END_INT | EPN_OUT_INT | EPN_IN_END_INT | EPN_IN_INT)

/*------- (0x0048:) EPN Interrupt Enable Register */
#define EPN_OUT_END_EN			BIT(23)		/* RW */
#define EPN_OUT_OR_EN			BIT(22)		/* RW */
#define EPN_OUT_NAK_ERR_EN		BIT(21)		/* RW */
#define EPN_OUT_STALL_EN		BIT(20)		/* RW */
#define EPN_OUT_EN			BIT(19)		/* RW */
#define EPN_OUT_NULL_EN			BIT(18)		/* RW */

#define EPN_IN_END_EN			BIT(7)		/* RW */

#define EPN_IN_NAK_ERR_EN		BIT(5)		/* RW */
#define EPN_IN_STALL_EN			BIT(4)		/* RW */
#define EPN_IN_EN			BIT(3)		/* RW */

/*------- (0x004C:) EPN Interrupt Enable Register */
#define EPN_STOP_MODE			BIT(11)
#define EPN_DEND_SET			BIT(10)
#define EPN_BURST_SET			BIT(9)
#define EPN_STOP_SET			BIT(8)

#define EPN_DMA_EN			BIT(4)

#define EPN_DMAMODE0			BIT(0)

/*------- (0x0050:) EPN MaxPacket & BaseAddress Register */
#define EPN_BASEAD			0x1FFF0000
#define EPN_MPKT			0x000007FF

/*------- (0x0054:) EPN Length & DMA Count Register */
#define EPN_DMACNT			0x01FF0000
#define EPN_LDATA			0x000007FF

/*------- (0x0058:) EPN Read Register */
/*------- (0x005C:) EPN Write Register */

/*------- (0x1000) AHBSCTR Register */
#define WAIT_MODE			BIT(0)

/*------- (0x1004) AHBMCTR Register */
#define ARBITER_CTR			BIT(31)		/* RW */
#define MCYCLE_RST			BIT(12)		/* RW */

#define ENDIAN_CTR			(BIT(9) | BIT(8))	/* RW */
#define ENDIAN_BYTE_SWAP		BIT(9)
#define ENDIAN_HALF_WORD_SWAP		ENDIAN_CTR

#define HBUSREQ_MODE			BIT(5)		/* RW */
#define HTRANS_MODE			BIT(4)		/* RW */

#define WBURST_TYPE			BIT(2)		/* RW */
#define BURST_TYPE			(BIT(1) | BIT(0))	/* RW */
#define BURST_MAX_16			0
#define BURST_MAX_8			BIT(0)
#define BURST_MAX_4			BIT(1)
#define BURST_SINGLE			BURST_TYPE

/*------- (0x1008) AHBBINT Register */
#define DMA_ENDINT			0xFFFE0000	/* RW */

#define AHB_VBUS_INT			BIT(13)		/* RW */

#define MBUS_ERRINT			BIT(6)		/* RW */

#define SBUS_ERRINT0			BIT(4)		/* RW */
#define ERR_MASTER			0x0000000F	/* R */

/*------- (0x100C) AHBBINTEN Register */
#define DMA_ENDINTEN			0xFFFE0000	/* RW */

#define VBUS_INTEN			BIT(13)		/* RW */

#define MBUS_ERRINTEN			BIT(6)		/* RW */

#define SBUS_ERRINT0EN			BIT(4)		/* RW */

/*------- (0x1010) EPCTR Register */
#define DIRPD				BIT(12)		/* RW */

#define VBUS_LEVEL			BIT(8)		/* R */

#define PLL_RESUME			BIT(5)		/* RW */
#define PLL_LOCK			BIT(4)		/* R */

#define EPC_RST				BIT(0)		/* RW */

/*------- (0x1014) USBF_EPTEST Register */
#define LINESTATE			(BIT(9) | BIT(8))	/* R */
#define DM_LEVEL			BIT(9)		/* R */
#define DP_LEVEL			BIT(8)		/* R */

#define PHY_TST				BIT(1)		/* RW */
#define PHY_TSTCLK			BIT(0)		/* RW */

/*------- (0x1020) USBSSVER Register */
#define AHBB_VER			0x00FF0000	/* R */
#define EPC_VER				0x0000FF00	/* R */
#define SS_VER				0x000000FF	/* R */

/*------- (0x1024) USBSSCONF Register */
#define EP_AVAILABLE			0xFFFF0000	/* R */
#define DMA_AVAILABLE			0x0000FFFF	/* R */

/*------- (0x1110:) EPNDCR1 Register */
#define DCR1_EPN_DMACNT			0x00FF0000	/* RW */

#define DCR1_EPN_DIR0			BIT(1)		/* RW */
#define DCR1_EPN_REQEN			BIT(0)		/* RW */

/*------- (0x1114:) EPNDCR2 Register */
#define DCR2_EPN_LMPKT			0x07FF0000	/* RW */

#define DCR2_EPN_MPKT			0x000007FF	/* RW */

/*------- (0x1118:) EPNTADR Register */
#define EPN_TADR			0xFFFFFFFF	/* RW */

/*===========================================================================*/
/* Struct */
/*------- ep_regs */
struct ep_regs {
	u32 EP_CONTROL;			/* EP Control */
	u32 EP_STATUS;			/* EP Status */
	u32 EP_INT_ENA;			/* EP Interrupt Enable */
	u32 EP_DMA_CTRL;		/* EP DMA Control */
	u32 EP_PCKT_ADRS;		/* EP Maxpacket & BaseAddress */
	u32 EP_LEN_DCNT;		/* EP Length & DMA count */
	u32 EP_READ;			/* EP Read */
	u32 EP_WRITE;			/* EP Write */
};

/*------- ep_dcr */
struct ep_dcr {
	u32 EP_DCR1;			/* EP_DCR1 */
	u32 EP_DCR2;			/* EP_DCR2 */
	u32 EP_TADR;			/* EP_TADR */
	u32 Reserved;			/* Reserved */
};

/*------- Function Registers */
struct fc_regs {
	u32 USB_CONTROL;		/* (0x0000) USB Control */
	u32 USB_STATUS;			/* (0x0004) USB Status */
	u32 USB_ADDRESS;		/* (0x0008) USB Address */
	u32 UTMI_CHARACTER_1;		/* (0x000C) UTMI Setting */
	u32 TEST_CONTROL;		/* (0x0010) TEST Control */
	u32 reserved_14;		/* (0x0014) Reserved */
	u32 SETUP_DATA0;		/* (0x0018) Setup Data0 */
	u32 SETUP_DATA1;		/* (0x001C) Setup Data1 */
	u32 USB_INT_STA;		/* (0x0020) USB Interrupt Status */
	u32 USB_INT_ENA;		/* (0x0024) USB Interrupt Enable */
	u32 EP0_CONTROL;		/* (0x0028) EP0 Control */
	u32 EP0_STATUS;			/* (0x002C) EP0 Status */
	u32 EP0_INT_ENA;		/* (0x0030) EP0 Interrupt Enable */
	u32 EP0_LENGTH;			/* (0x0034) EP0 Length */
	u32 EP0_READ;			/* (0x0038) EP0 Read */
	u32 EP0_WRITE;			/* (0x003C) EP0 Write */

	struct ep_regs EP_REGS[REG_EP_NUM];	/* Endpoint Register */

	u8 reserved_220[0x1000 - 0x220];	/* (0x0220:0x0FFF) Reserved */

	u32 AHBSCTR;			/* (0x1000) AHBSCTR */
	u32 AHBMCTR;			/* (0x1004) AHBMCTR */
	u32 AHBBINT;			/* (0x1008) AHBBINT */
	u32 AHBBINTEN;			/* (0x100C) AHBBINTEN */
	u32 EPCTR;			/* (0x1010) EPCTR */
	u32 USBF_EPTEST;		/* (0x1014) USBF_EPTEST */

	u8 reserved_1018[0x20 - 0x18];	/* (0x1018:0x101F) Reserved */

	u32 USBSSVER;			/* (0x1020) USBSSVER */
	u32 USBSSCONF;			/* (0x1024) USBSSCONF */

	u8 reserved_1028[0x110 - 0x28];	/* (0x1028:0x110F) Reserved */

	struct ep_dcr EP_DCR[REG_EP_NUM];	/* */

	u8 reserved_1200[0x1000 - 0x200];	/* Reserved */
} __aligned(32);

#define EP0_PACKETSIZE			64
#define EP_PACKETSIZE			1024

/* EPN RAM SIZE */
#define D_RAM_SIZE_CTRL			64

/* EPN Bulk Endpoint Max Packet Size */
#define D_FS_RAM_SIZE_BULK		64
#define D_HS_RAM_SIZE_BULK		512

struct nbu2ss_udc;

enum ep0_state {
	EP0_IDLE,
	EP0_IN_DATA_PHASE,
	EP0_OUT_DATA_PHASE,
	EP0_IN_STATUS_PHASE,
	EP0_OUT_STATUS_PAHSE,
	EP0_END_XFER,
	EP0_SUSPEND,
	EP0_STALL,
};

struct nbu2ss_req {
	struct usb_request		req;
	struct list_head		queue;

	u32			div_len;
	bool		dma_flag;
	bool		zero;

	bool		unaligned;

	unsigned			mapped:1;
};

struct nbu2ss_ep {
	struct usb_ep			ep;
	struct list_head		queue;

	struct nbu2ss_udc		*udc;

	const struct usb_endpoint_descriptor *desc;

	u8		epnum;
	u8		direct;
	u8		ep_type;

	unsigned		wedged:1;
	unsigned		halted:1;
	unsigned		stalled:1;

	u8		*virt_buf;
	dma_addr_t	phys_buf;
};

struct nbu2ss_udc {
	struct usb_gadget gadget;
	struct usb_gadget_driver *driver;
	struct platform_device *pdev;
	struct device *dev;
	spinlock_t lock; /* Protects nbu2ss_udc structure fields */
	struct completion		*pdone;

	enum ep0_state			ep0state;
	enum usb_device_state	devstate;
	struct usb_ctrlrequest	ctrl;
	struct nbu2ss_req		ep0_req;
	u8		ep0_buf[EP0_PACKETSIZE];

	struct nbu2ss_ep	ep[NUM_ENDPOINTS];

	unsigned		softconnect:1;
	unsigned		vbus_active:1;
	unsigned		linux_suspended:1;
	unsigned		linux_resume:1;
	unsigned		usb_suspended:1;
	unsigned		remote_wakeup:1;
	unsigned		udc_enabled:1;

	unsigned int		mA;

	u32		curr_config;	/* Current Configuration Number */

	struct fc_regs __iomem *p_regs;
};

/* USB register access structure */
union usb_reg_access {
	struct {
		unsigned char	DATA[4];
	} byte;
	unsigned int		dw;
};

/*-------------------------------------------------------------------------*/

#endif  /* _LINUX_EMXX_H */
