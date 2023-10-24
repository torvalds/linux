// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas USBF USB Function driver
 *
 * Copyright 2022 Schneider Electric
 * Author: Herve Codina <herve.codina@bootlin.com>
 */

#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/kfifo.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/types.h>
#include <linux/usb/composite.h>
#include <linux/usb/gadget.h>
#include <linux/usb/role.h>

#define USBF_NUM_ENDPOINTS	16
#define USBF_EP0_MAX_PCKT_SIZE	64

/* EPC registers */
#define USBF_REG_USB_CONTROL	0x000
#define     USBF_USB_PUE2		BIT(2)
#define     USBF_USB_CONNECTB		BIT(3)
#define     USBF_USB_DEFAULT		BIT(4)
#define     USBF_USB_CONF		BIT(5)
#define     USBF_USB_SUSPEND		BIT(6)
#define     USBF_USB_RSUM_IN		BIT(7)
#define     USBF_USB_SOF_RCV		BIT(8)
#define     USBF_USB_FORCEFS		BIT(9)
#define     USBF_USB_INT_SEL		BIT(10)
#define     USBF_USB_SOF_CLK_MODE	BIT(11)

#define USBF_REG_USB_STATUS	0x004
#define     USBF_USB_RSUM_OUT		BIT(1)
#define     USBF_USB_SPND_OUT		BIT(2)
#define     USBF_USB_USB_RST		BIT(3)
#define     USBF_USB_DEFAULT_ST		BIT(4)
#define     USBF_USB_CONF_ST		BIT(5)
#define     USBF_USB_SPEED_MODE		BIT(6)
#define     USBF_USB_SOF_DELAY_STATUS	BIT(31)

#define USBF_REG_USB_ADDRESS	0x008
#define     USBF_USB_SOF_STATUS		BIT(15)
#define     USBF_USB_SET_USB_ADDR(_a)	((_a) << 16)
#define     USBF_USB_GET_FRAME(_r)	((_r) & 0x7FF)

#define USBF_REG_SETUP_DATA0	0x018
#define USBF_REG_SETUP_DATA1	0x01C
#define USBF_REG_USB_INT_STA	0x020
#define     USBF_USB_RSUM_INT		BIT(1)
#define     USBF_USB_SPND_INT		BIT(2)
#define     USBF_USB_USB_RST_INT	BIT(3)
#define     USBF_USB_SOF_INT		BIT(4)
#define     USBF_USB_SOF_ERROR_INT	BIT(5)
#define     USBF_USB_SPEED_MODE_INT	BIT(6)
#define     USBF_USB_EPN_INT(_n)	(BIT(8) << (_n)) /* n=0..15 */

#define USBF_REG_USB_INT_ENA	0x024
#define     USBF_USB_RSUM_EN		BIT(1)
#define     USBF_USB_SPND_EN		BIT(2)
#define     USBF_USB_USB_RST_EN		BIT(3)
#define     USBF_USB_SOF_EN		BIT(4)
#define     USBF_USB_SOF_ERROR_EN	BIT(5)
#define     USBF_USB_SPEED_MODE_EN	BIT(6)
#define     USBF_USB_EPN_EN(_n)		(BIT(8) << (_n)) /* n=0..15 */

#define USBF_BASE_EP0		0x028
/* EP0 registers offsets from Base + USBF_BASE_EP0 (EP0 regs area) */
#define     USBF_REG_EP0_CONTROL	0x00
#define         USBF_EP0_ONAK			BIT(0)
#define         USBF_EP0_INAK			BIT(1)
#define         USBF_EP0_STL			BIT(2)
#define         USBF_EP0_PERR_NAK_CLR		BIT(3)
#define         USBF_EP0_INAK_EN		BIT(4)
#define         USBF_EP0_DW_MASK		(0x3 << 5)
#define         USBF_EP0_DW(_s)			((_s) << 5)
#define         USBF_EP0_DEND			BIT(7)
#define         USBF_EP0_BCLR			BIT(8)
#define         USBF_EP0_PIDCLR			BIT(9)
#define         USBF_EP0_AUTO			BIT(16)
#define         USBF_EP0_OVERSEL		BIT(17)
#define         USBF_EP0_STGSEL			BIT(18)

#define     USBF_REG_EP0_STATUS		0x04
#define         USBF_EP0_SETUP_INT		BIT(0)
#define         USBF_EP0_STG_START_INT		BIT(1)
#define         USBF_EP0_STG_END_INT		BIT(2)
#define         USBF_EP0_STALL_INT		BIT(3)
#define         USBF_EP0_IN_INT			BIT(4)
#define         USBF_EP0_OUT_INT		BIT(5)
#define         USBF_EP0_OUT_OR_INT		BIT(6)
#define         USBF_EP0_OUT_NULL_INT		BIT(7)
#define         USBF_EP0_IN_EMPTY		BIT(8)
#define         USBF_EP0_IN_FULL		BIT(9)
#define         USBF_EP0_IN_DATA		BIT(10)
#define         USBF_EP0_IN_NAK_INT		BIT(11)
#define         USBF_EP0_OUT_EMPTY		BIT(12)
#define         USBF_EP0_OUT_FULL		BIT(13)
#define         USBF_EP0_OUT_NULL		BIT(14)
#define         USBF_EP0_OUT_NAK_INT		BIT(15)
#define         USBF_EP0_PERR_NAK_INT		BIT(16)
#define         USBF_EP0_PERR_NAK		BIT(17)
#define         USBF_EP0_PID			BIT(18)

#define     USBF_REG_EP0_INT_ENA	0x08
#define         USBF_EP0_SETUP_EN		BIT(0)
#define         USBF_EP0_STG_START_EN		BIT(1)
#define         USBF_EP0_STG_END_EN		BIT(2)
#define         USBF_EP0_STALL_EN		BIT(3)
#define         USBF_EP0_IN_EN			BIT(4)
#define         USBF_EP0_OUT_EN			BIT(5)
#define         USBF_EP0_OUT_OR_EN		BIT(6)
#define         USBF_EP0_OUT_NULL_EN		BIT(7)
#define         USBF_EP0_IN_NAK_EN		BIT(11)
#define         USBF_EP0_OUT_NAK_EN		BIT(15)
#define         USBF_EP0_PERR_NAK_EN		BIT(16)

#define     USBF_REG_EP0_LENGTH		0x0C
#define         USBF_EP0_LDATA			(0x7FF << 0)
#define     USBF_REG_EP0_READ		0x10
#define     USBF_REG_EP0_WRITE		0x14

#define USBF_BASE_EPN(_n)	(0x040 + (_n) * 0x020)
/* EPn registers offsets from Base + USBF_BASE_EPN(n-1). n=1..15 */
#define     USBF_REG_EPN_CONTROL	0x000
#define         USBF_EPN_ONAK			BIT(0)
#define         USBF_EPN_OSTL			BIT(2)
#define         USBF_EPN_ISTL			BIT(3)
#define         USBF_EPN_OSTL_EN		BIT(4)
#define         USBF_EPN_DW_MASK		(0x3 << 5)
#define         USBF_EPN_DW(_s)			((_s) << 5)
#define         USBF_EPN_DEND			BIT(7)
#define         USBF_EPN_CBCLR			BIT(8)
#define         USBF_EPN_BCLR			BIT(9)
#define         USBF_EPN_OPIDCLR		BIT(10)
#define         USBF_EPN_IPIDCLR		BIT(11)
#define         USBF_EPN_AUTO			BIT(16)
#define         USBF_EPN_OVERSEL		BIT(17)
#define         USBF_EPN_MODE_MASK		(0x3 << 24)
#define         USBF_EPN_MODE_BULK		(0x0 << 24)
#define         USBF_EPN_MODE_INTR		(0x1 << 24)
#define         USBF_EPN_MODE_ISO		(0x2 << 24)
#define         USBF_EPN_DIR0			BIT(26)
#define         USBF_EPN_BUF_TYPE_DOUBLE	BIT(30)
#define         USBF_EPN_EN			BIT(31)

#define     USBF_REG_EPN_STATUS		0x004
#define         USBF_EPN_IN_EMPTY		BIT(0)
#define         USBF_EPN_IN_FULL		BIT(1)
#define         USBF_EPN_IN_DATA		BIT(2)
#define         USBF_EPN_IN_INT			BIT(3)
#define         USBF_EPN_IN_STALL_INT		BIT(4)
#define         USBF_EPN_IN_NAK_ERR_INT		BIT(5)
#define         USBF_EPN_IN_END_INT		BIT(7)
#define         USBF_EPN_IPID			BIT(10)
#define         USBF_EPN_OUT_EMPTY		BIT(16)
#define         USBF_EPN_OUT_FULL		BIT(17)
#define         USBF_EPN_OUT_NULL_INT		BIT(18)
#define         USBF_EPN_OUT_INT		BIT(19)
#define         USBF_EPN_OUT_STALL_INT		BIT(20)
#define         USBF_EPN_OUT_NAK_ERR_INT	BIT(21)
#define         USBF_EPN_OUT_OR_INT		BIT(22)
#define         USBF_EPN_OUT_END_INT		BIT(23)
#define         USBF_EPN_ISO_CRC		BIT(24)
#define         USBF_EPN_ISO_OR			BIT(26)
#define         USBF_EPN_OUT_NOTKN		BIT(27)
#define         USBF_EPN_ISO_OPID		BIT(28)
#define         USBF_EPN_ISO_PIDERR		BIT(29)

#define     USBF_REG_EPN_INT_ENA	0x008
#define         USBF_EPN_IN_EN			BIT(3)
#define         USBF_EPN_IN_STALL_EN		BIT(4)
#define         USBF_EPN_IN_NAK_ERR_EN		BIT(5)
#define         USBF_EPN_IN_END_EN		BIT(7)
#define         USBF_EPN_OUT_NULL_EN		BIT(18)
#define         USBF_EPN_OUT_EN			BIT(19)
#define         USBF_EPN_OUT_STALL_EN		BIT(20)
#define         USBF_EPN_OUT_NAK_ERR_EN		BIT(21)
#define         USBF_EPN_OUT_OR_EN		BIT(22)
#define         USBF_EPN_OUT_END_EN		BIT(23)

#define     USBF_REG_EPN_DMA_CTRL	0x00C
#define         USBF_EPN_DMAMODE0		BIT(0)
#define         USBF_EPN_DMA_EN			BIT(4)
#define         USBF_EPN_STOP_SET		BIT(8)
#define         USBF_EPN_BURST_SET		BIT(9)
#define         USBF_EPN_DEND_SET		BIT(10)
#define         USBF_EPN_STOP_MODE		BIT(11)

#define     USBF_REG_EPN_PCKT_ADRS	0x010
#define         USBF_EPN_MPKT(_l)		((_l) << 0)
#define         USBF_EPN_BASEAD(_a)		((_a) << 16)

#define     USBF_REG_EPN_LEN_DCNT	0x014
#define         USBF_EPN_GET_LDATA(_r)		((_r) & 0x7FF)
#define         USBF_EPN_SET_DMACNT(_c)		((_c) << 16)
#define         USBF_EPN_GET_DMACNT(_r)		(((_r) >> 16) & 0x1ff)

#define     USBF_REG_EPN_READ		0x018
#define     USBF_REG_EPN_WRITE		0x01C

/* AHB-EPC Bridge registers */
#define USBF_REG_AHBSCTR	0x1000
#define USBF_REG_AHBMCTR	0x1004
#define     USBF_SYS_WBURST_TYPE	BIT(2)
#define     USBF_SYS_ARBITER_CTR	BIT(31)

#define USBF_REG_AHBBINT	0x1008
#define     USBF_SYS_ERR_MASTER		 (0x0F << 0)
#define     USBF_SYS_SBUS_ERRINT0	 BIT(4)
#define     USBF_SYS_SBUS_ERRINT1	 BIT(5)
#define     USBF_SYS_MBUS_ERRINT	 BIT(6)
#define     USBF_SYS_VBUS_INT		 BIT(13)
#define     USBF_SYS_DMA_ENDINT_EPN(_n)	 (BIT(16) << (_n)) /* _n=1..15 */

#define USBF_REG_AHBBINTEN	0x100C
#define     USBF_SYS_SBUS_ERRINT0EN	  BIT(4)
#define     USBF_SYS_SBUS_ERRINT1EN	  BIT(5)
#define     USBF_SYS_MBUS_ERRINTEN	  BIT(6)
#define     USBF_SYS_VBUS_INTEN		  BIT(13)
#define     USBF_SYS_DMA_ENDINTEN_EPN(_n) (BIT(16) << (_n)) /* _n=1..15 */

#define USBF_REG_EPCTR		0x1010
#define     USBF_SYS_EPC_RST		BIT(0)
#define     USBF_SYS_PLL_RST		BIT(2)
#define     USBF_SYS_PLL_LOCK		BIT(4)
#define     USBF_SYS_PLL_RESUME		BIT(5)
#define     USBF_SYS_VBUS_LEVEL		BIT(8)
#define     USBF_SYS_DIRPD		BIT(12)

#define USBF_REG_USBSSVER	0x1020
#define USBF_REG_USBSSCONF	0x1024
#define    USBF_SYS_DMA_AVAILABLE(_n)	(BIT(0) << (_n)) /* _n=0..15 */
#define    USBF_SYS_EP_AVAILABLE(_n)	(BIT(16) << (_n)) /* _n=0..15 */

#define USBF_BASE_DMA_EPN(_n)	(0x1110 + (_n) * 0x010)
/* EPn DMA registers offsets from Base USBF_BASE_DMA_EPN(n-1). n=1..15*/
#define     USBF_REG_DMA_EPN_DCR1	0x00
#define         USBF_SYS_EPN_REQEN		BIT(0)
#define         USBF_SYS_EPN_DIR0		BIT(1)
#define         USBF_SYS_EPN_SET_DMACNT(_c)	((_c) << 16)
#define         USBF_SYS_EPN_GET_DMACNT(_r)	(((_r) >> 16) & 0x0FF)

#define     USBF_REG_DMA_EPN_DCR2	0x04
#define         USBF_SYS_EPN_MPKT(_s)		((_s) << 0)
#define         USBF_SYS_EPN_LMPKT(_l)		((_l) << 16)

#define     USBF_REG_DMA_EPN_TADR	0x08

/* USB request */
struct usbf_req {
	struct usb_request	req;
	struct list_head	queue;
	unsigned int		is_zero_sent : 1;
	unsigned int		is_mapped : 1;
	enum {
		USBF_XFER_START,
		USBF_XFER_WAIT_DMA,
		USBF_XFER_SEND_NULL,
		USBF_XFER_WAIT_END,
		USBF_XFER_WAIT_DMA_SHORT,
		USBF_XFER_WAIT_BRIDGE,
	}			xfer_step;
	size_t			dma_size;
};

/* USB Endpoint */
struct usbf_ep {
	struct usb_ep		ep;
	char			name[32];
	struct list_head	queue;
	unsigned int		is_processing : 1;
	unsigned int		is_in : 1;
	struct			usbf_udc *udc;
	void __iomem		*regs;
	void __iomem		*dma_regs;
	unsigned int		id : 8;
	unsigned int		disabled : 1;
	unsigned int		is_wedged : 1;
	unsigned int		delayed_status : 1;
	u32			status;
	void			(*bridge_on_dma_end)(struct usbf_ep *ep);
};

enum usbf_ep0state {
	EP0_IDLE,
	EP0_IN_DATA_PHASE,
	EP0_OUT_DATA_PHASE,
	EP0_OUT_STATUS_START_PHASE,
	EP0_OUT_STATUS_PHASE,
	EP0_OUT_STATUS_END_PHASE,
	EP0_IN_STATUS_START_PHASE,
	EP0_IN_STATUS_PHASE,
	EP0_IN_STATUS_END_PHASE,
};

struct usbf_udc {
	struct usb_gadget		gadget;
	struct usb_gadget_driver	*driver;
	struct device			*dev;
	void __iomem			*regs;
	spinlock_t			lock;
	bool				is_remote_wakeup;
	bool				is_usb_suspended;
	struct usbf_ep			ep[USBF_NUM_ENDPOINTS];
	/* for EP0 control messages */
	enum usbf_ep0state		ep0state;
	struct usbf_req			setup_reply;
	u8				ep0_buf[USBF_EP0_MAX_PCKT_SIZE];
};

struct usbf_ep_info {
	const char		*name;
	struct usb_ep_caps	caps;
	u16			base_addr;
	unsigned int		is_double : 1;
	u16			maxpacket_limit;
};

#define USBF_SINGLE_BUFFER 0
#define USBF_DOUBLE_BUFFER 1
#define USBF_EP_INFO(_name, _caps, _base_addr, _is_double, _maxpacket_limit)  \
	{                                                                     \
		.name            = _name,                                     \
		.caps            = _caps,                                     \
		.base_addr       = _base_addr,                                \
		.is_double       = _is_double,                                \
		.maxpacket_limit = _maxpacket_limit,                          \
	}

/* This table is computed from the recommended values provided in the SOC
 * datasheet. The buffer type (single/double) and the endpoint type cannot
 * be changed. The mapping in internal RAM (base_addr and number of words)
 * for each endpoints depends on the max packet size and the buffer type.
 */
static const struct usbf_ep_info usbf_ep_info[USBF_NUM_ENDPOINTS] = {
	/* ep0: buf @0x0000 64 bytes, fixed 32 words */
	[0] = USBF_EP_INFO("ep0-ctrl",
			   USB_EP_CAPS(USB_EP_CAPS_TYPE_CONTROL,
				       USB_EP_CAPS_DIR_ALL),
			   0x0000, USBF_SINGLE_BUFFER, USBF_EP0_MAX_PCKT_SIZE),
	/* ep1: buf @0x0020, 2 buffers 512 bytes -> (512 * 2 / 4) words */
	[1] = USBF_EP_INFO("ep1-bulk",
			   USB_EP_CAPS(USB_EP_CAPS_TYPE_BULK,
				       USB_EP_CAPS_DIR_ALL),
			   0x0020, USBF_DOUBLE_BUFFER, 512),
	/* ep2: buf @0x0120, 2 buffers 512 bytes -> (512 * 2 / 4) words */
	[2] = USBF_EP_INFO("ep2-bulk",
			   USB_EP_CAPS(USB_EP_CAPS_TYPE_BULK,
				       USB_EP_CAPS_DIR_ALL),
			   0x0120, USBF_DOUBLE_BUFFER, 512),
	/* ep3: buf @0x0220, 1 buffer 512 bytes -> (512 * 2 / 4) words */
	[3] = USBF_EP_INFO("ep3-bulk",
			   USB_EP_CAPS(USB_EP_CAPS_TYPE_BULK,
				       USB_EP_CAPS_DIR_ALL),
			   0x0220, USBF_SINGLE_BUFFER, 512),
	/* ep4: buf @0x02A0, 1 buffer 512 bytes -> (512 * 1 / 4) words */
	[4] = USBF_EP_INFO("ep4-bulk",
			   USB_EP_CAPS(USB_EP_CAPS_TYPE_BULK,
				       USB_EP_CAPS_DIR_ALL),
			   0x02A0, USBF_SINGLE_BUFFER, 512),
	/* ep5: buf @0x0320, 1 buffer 512 bytes -> (512 * 2 / 4) words */
	[5] = USBF_EP_INFO("ep5-bulk",
			   USB_EP_CAPS(USB_EP_CAPS_TYPE_BULK,
				       USB_EP_CAPS_DIR_ALL),
			   0x0320, USBF_SINGLE_BUFFER, 512),
	/* ep6: buf @0x03A0, 1 buffer 1024 bytes -> (1024 * 1 / 4) words */
	[6] = USBF_EP_INFO("ep6-int",
			   USB_EP_CAPS(USB_EP_CAPS_TYPE_INT,
				       USB_EP_CAPS_DIR_ALL),
			   0x03A0, USBF_SINGLE_BUFFER, 1024),
	/* ep7: buf @0x04A0, 1 buffer 1024 bytes -> (1024 * 1 / 4) words */
	[7] = USBF_EP_INFO("ep7-int",
			   USB_EP_CAPS(USB_EP_CAPS_TYPE_INT,
				       USB_EP_CAPS_DIR_ALL),
			   0x04A0, USBF_SINGLE_BUFFER, 1024),
	/* ep8: buf @0x0520, 1 buffer 1024 bytes -> (1024 * 1 / 4) words */
	[8] = USBF_EP_INFO("ep8-int",
			   USB_EP_CAPS(USB_EP_CAPS_TYPE_INT,
				       USB_EP_CAPS_DIR_ALL),
			   0x0520, USBF_SINGLE_BUFFER, 1024),
	/* ep9: buf @0x0620, 1 buffer 1024 bytes -> (1024 * 1 / 4) words */
	[9] = USBF_EP_INFO("ep9-int",
			   USB_EP_CAPS(USB_EP_CAPS_TYPE_INT,
				       USB_EP_CAPS_DIR_ALL),
			   0x0620, USBF_SINGLE_BUFFER, 1024),
	/* ep10: buf @0x0720, 2 buffers 1024 bytes -> (1024 * 2 / 4) words */
	[10] = USBF_EP_INFO("ep10-iso",
			    USB_EP_CAPS(USB_EP_CAPS_TYPE_ISO,
					USB_EP_CAPS_DIR_ALL),
			    0x0720, USBF_DOUBLE_BUFFER, 1024),
	/* ep11: buf @0x0920, 2 buffers 1024 bytes -> (1024 * 2 / 4) words */
	[11] = USBF_EP_INFO("ep11-iso",
			    USB_EP_CAPS(USB_EP_CAPS_TYPE_ISO,
					USB_EP_CAPS_DIR_ALL),
			    0x0920, USBF_DOUBLE_BUFFER, 1024),
	/* ep12: buf @0x0B20, 2 buffers 1024 bytes -> (1024 * 2 / 4) words */
	[12] = USBF_EP_INFO("ep12-iso",
			    USB_EP_CAPS(USB_EP_CAPS_TYPE_ISO,
					USB_EP_CAPS_DIR_ALL),
			    0x0B20, USBF_DOUBLE_BUFFER, 1024),
	/* ep13: buf @0x0D20, 2 buffers 1024 bytes -> (1024 * 2 / 4) words */
	[13] = USBF_EP_INFO("ep13-iso",
			    USB_EP_CAPS(USB_EP_CAPS_TYPE_ISO,
					USB_EP_CAPS_DIR_ALL),
			    0x0D20, USBF_DOUBLE_BUFFER, 1024),
	/* ep14: buf @0x0F20, 2 buffers 1024 bytes -> (1024 * 2 / 4) words */
	[14] = USBF_EP_INFO("ep14-iso",
			    USB_EP_CAPS(USB_EP_CAPS_TYPE_ISO,
					USB_EP_CAPS_DIR_ALL),
			    0x0F20, USBF_DOUBLE_BUFFER, 1024),
	/* ep15: buf @0x1120, 2 buffers 1024 bytes -> (1024 * 2 / 4) words */
	[15] = USBF_EP_INFO("ep15-iso",
			    USB_EP_CAPS(USB_EP_CAPS_TYPE_ISO,
					USB_EP_CAPS_DIR_ALL),
			    0x1120, USBF_DOUBLE_BUFFER, 1024),
};

static inline u32 usbf_reg_readl(struct usbf_udc *udc, uint offset)
{
	return readl(udc->regs + offset);
}

static inline void usbf_reg_writel(struct usbf_udc *udc, uint offset, u32 val)
{
	writel(val, udc->regs + offset);
}

static inline void usbf_reg_bitset(struct usbf_udc *udc, uint offset, u32 set)
{
	u32 tmp;

	tmp = usbf_reg_readl(udc, offset);
	tmp |= set;
	usbf_reg_writel(udc, offset, tmp);
}

static inline void usbf_reg_bitclr(struct usbf_udc *udc, uint offset, u32 clr)
{
	u32 tmp;

	tmp = usbf_reg_readl(udc, offset);
	tmp &= ~clr;
	usbf_reg_writel(udc, offset, tmp);
}

static inline void usbf_reg_clrset(struct usbf_udc *udc, uint offset,
				   u32 clr, u32 set)
{
	u32 tmp;

	tmp = usbf_reg_readl(udc, offset);
	tmp &= ~clr;
	tmp |= set;
	usbf_reg_writel(udc, offset, tmp);
}

static inline u32 usbf_ep_reg_readl(struct usbf_ep *ep, uint offset)
{
	return readl(ep->regs + offset);
}

static inline void usbf_ep_reg_read_rep(struct usbf_ep *ep, uint offset,
				       void *dst, uint count)
{
	readsl(ep->regs + offset, dst, count);
}

static inline void usbf_ep_reg_writel(struct usbf_ep *ep, uint offset, u32 val)
{
	writel(val, ep->regs + offset);
}

static inline void usbf_ep_reg_write_rep(struct usbf_ep *ep, uint offset,
					 const void *src, uint count)
{
	writesl(ep->regs + offset, src, count);
}

static inline void usbf_ep_reg_bitset(struct usbf_ep *ep, uint offset, u32 set)
{
	u32 tmp;

	tmp = usbf_ep_reg_readl(ep, offset);
	tmp |= set;
	usbf_ep_reg_writel(ep, offset, tmp);
}

static inline void usbf_ep_reg_bitclr(struct usbf_ep *ep, uint offset, u32 clr)
{
	u32 tmp;

	tmp = usbf_ep_reg_readl(ep, offset);
	tmp &= ~clr;
	usbf_ep_reg_writel(ep, offset, tmp);
}

static inline void usbf_ep_reg_clrset(struct usbf_ep *ep, uint offset,
				      u32 clr, u32 set)
{
	u32 tmp;

	tmp = usbf_ep_reg_readl(ep, offset);
	tmp &= ~clr;
	tmp |= set;
	usbf_ep_reg_writel(ep, offset, tmp);
}

static inline u32 usbf_ep_dma_reg_readl(struct usbf_ep *ep, uint offset)
{
	return readl(ep->dma_regs + offset);
}

static inline void usbf_ep_dma_reg_writel(struct usbf_ep *ep, uint offset,
					  u32 val)
{
	writel(val, ep->dma_regs + offset);
}

static inline void usbf_ep_dma_reg_bitset(struct usbf_ep *ep, uint offset,
					  u32 set)
{
	u32 tmp;

	tmp = usbf_ep_dma_reg_readl(ep, offset);
	tmp |= set;
	usbf_ep_dma_reg_writel(ep, offset, tmp);
}

static inline void usbf_ep_dma_reg_bitclr(struct usbf_ep *ep, uint offset,
					  u32 clr)
{
	u32 tmp;

	tmp = usbf_ep_dma_reg_readl(ep, offset);
	tmp &= ~clr;
	usbf_ep_dma_reg_writel(ep, offset, tmp);
}

static void usbf_ep0_send_null(struct usbf_ep *ep0, bool is_data1)
{
	u32 set;

	set = USBF_EP0_DEND;
	if (is_data1)
		set |= USBF_EP0_PIDCLR;

	usbf_ep_reg_bitset(ep0, USBF_REG_EP0_CONTROL, set);
}

static int usbf_ep0_pio_in(struct usbf_ep *ep0, struct usbf_req *req)
{
	unsigned int left;
	unsigned int nb;
	const void *buf;
	u32 ctrl;
	u32 last;

	left = req->req.length - req->req.actual;

	if (left == 0) {
		if (!req->is_zero_sent) {
			if (req->req.length == 0) {
				dev_dbg(ep0->udc->dev, "ep0 send null\n");
				usbf_ep0_send_null(ep0, false);
				req->is_zero_sent = 1;
				return -EINPROGRESS;
			}
			if ((req->req.actual % ep0->ep.maxpacket) == 0) {
				if (req->req.zero) {
					dev_dbg(ep0->udc->dev, "ep0 send null\n");
					usbf_ep0_send_null(ep0, false);
					req->is_zero_sent = 1;
					return -EINPROGRESS;
				}
			}
		}
		return 0;
	}

	if (left > ep0->ep.maxpacket)
		left = ep0->ep.maxpacket;

	buf = req->req.buf;
	buf += req->req.actual;

	nb = left / sizeof(u32);
	if (nb) {
		usbf_ep_reg_write_rep(ep0, USBF_REG_EP0_WRITE, buf, nb);
		buf += (nb * sizeof(u32));
		req->req.actual += (nb * sizeof(u32));
		left -= (nb * sizeof(u32));
	}
	ctrl = usbf_ep_reg_readl(ep0, USBF_REG_EP0_CONTROL);
	ctrl &= ~USBF_EP0_DW_MASK;
	if (left) {
		memcpy(&last, buf, left);
		usbf_ep_reg_writel(ep0, USBF_REG_EP0_WRITE, last);
		ctrl |= USBF_EP0_DW(left);
		req->req.actual += left;
	}
	usbf_ep_reg_writel(ep0, USBF_REG_EP0_CONTROL, ctrl | USBF_EP0_DEND);

	dev_dbg(ep0->udc->dev, "ep0 send %u/%u\n",
		req->req.actual, req->req.length);

	return -EINPROGRESS;
}

static int usbf_ep0_pio_out(struct usbf_ep *ep0, struct usbf_req *req)
{
	int req_status = 0;
	unsigned int count;
	unsigned int recv;
	unsigned int left;
	unsigned int nb;
	void *buf;
	u32 last;

	if (ep0->status & USBF_EP0_OUT_INT) {
		recv = usbf_ep_reg_readl(ep0, USBF_REG_EP0_LENGTH) & USBF_EP0_LDATA;
		count = recv;

		buf = req->req.buf;
		buf += req->req.actual;

		left = req->req.length - req->req.actual;

		dev_dbg(ep0->udc->dev, "ep0 recv %u, left %u\n", count, left);

		if (left > ep0->ep.maxpacket)
			left = ep0->ep.maxpacket;

		if (count > left) {
			req_status = -EOVERFLOW;
			count = left;
		}

		if (count) {
			nb = count / sizeof(u32);
			if (nb) {
				usbf_ep_reg_read_rep(ep0, USBF_REG_EP0_READ,
					buf, nb);
				buf += (nb * sizeof(u32));
				req->req.actual += (nb * sizeof(u32));
				count -= (nb * sizeof(u32));
			}
			if (count) {
				last = usbf_ep_reg_readl(ep0, USBF_REG_EP0_READ);
				memcpy(buf, &last, count);
				req->req.actual += count;
			}
		}
		dev_dbg(ep0->udc->dev, "ep0 recv %u/%u\n",
			req->req.actual, req->req.length);

		if (req_status) {
			dev_dbg(ep0->udc->dev, "ep0 req.status=%d\n", req_status);
			req->req.status = req_status;
			return 0;
		}

		if (recv < ep0->ep.maxpacket) {
			dev_dbg(ep0->udc->dev, "ep0 short packet\n");
			/* This is a short packet -> It is the end */
			req->req.status = 0;
			return 0;
		}

		/* The Data stage of a control transfer from an endpoint to the
		 * host is complete when the endpoint does one of the following:
		 * - Has transferred exactly the expected amount of data
		 * - Transfers a packet with a payload size less than
		 *   wMaxPacketSize or transfers a zero-length packet
		 */
		if (req->req.actual == req->req.length) {
			req->req.status = 0;
			return 0;
		}
	}

	if (ep0->status & USBF_EP0_OUT_NULL_INT) {
		/* NULL packet received */
		dev_dbg(ep0->udc->dev, "ep0 null packet\n");
		if (req->req.actual != req->req.length) {
			req->req.status = req->req.short_not_ok ?
					  -EREMOTEIO : 0;
		} else {
			req->req.status = 0;
		}
		return 0;
	}

	return -EINPROGRESS;
}

static void usbf_ep0_fifo_flush(struct usbf_ep *ep0)
{
	u32 sts;
	int ret;

	usbf_ep_reg_bitset(ep0, USBF_REG_EP0_CONTROL, USBF_EP0_BCLR);

	ret = readl_poll_timeout_atomic(ep0->regs + USBF_REG_EP0_STATUS, sts,
		(sts & (USBF_EP0_IN_DATA | USBF_EP0_IN_EMPTY)) == USBF_EP0_IN_EMPTY,
		0,  10000);
	if (ret)
		dev_err(ep0->udc->dev, "ep0 flush fifo timed out\n");

}

static void usbf_epn_send_null(struct usbf_ep *epn)
{
	usbf_ep_reg_bitset(epn, USBF_REG_EPN_CONTROL, USBF_EPN_DEND);
}

static void usbf_epn_send_residue(struct usbf_ep *epn, const void *buf,
				  unsigned int size)
{
	u32 tmp;

	memcpy(&tmp, buf, size);
	usbf_ep_reg_writel(epn, USBF_REG_EPN_WRITE, tmp);

	usbf_ep_reg_clrset(epn, USBF_REG_EPN_CONTROL,
				USBF_EPN_DW_MASK,
				USBF_EPN_DW(size) | USBF_EPN_DEND);
}

static int usbf_epn_pio_in(struct usbf_ep *epn, struct usbf_req *req)
{
	unsigned int left;
	unsigned int nb;
	const void *buf;

	left = req->req.length - req->req.actual;

	if (left == 0) {
		if (!req->is_zero_sent) {
			if (req->req.length == 0) {
				dev_dbg(epn->udc->dev, "ep%u send_null\n", epn->id);
				usbf_epn_send_null(epn);
				req->is_zero_sent = 1;
				return -EINPROGRESS;
			}
			if ((req->req.actual % epn->ep.maxpacket) == 0) {
				if (req->req.zero) {
					dev_dbg(epn->udc->dev, "ep%u send_null\n",
						epn->id);
					usbf_epn_send_null(epn);
					req->is_zero_sent = 1;
					return -EINPROGRESS;
				}
			}
		}
		return 0;
	}

	if (left > epn->ep.maxpacket)
		left = epn->ep.maxpacket;

	buf = req->req.buf;
	buf += req->req.actual;

	nb = left / sizeof(u32);
	if (nb) {
		usbf_ep_reg_write_rep(epn, USBF_REG_EPN_WRITE, buf, nb);
		buf += (nb * sizeof(u32));
		req->req.actual += (nb * sizeof(u32));
		left -= (nb * sizeof(u32));
	}

	if (left) {
		usbf_epn_send_residue(epn, buf, left);
		req->req.actual += left;
	} else {
		usbf_ep_reg_clrset(epn, USBF_REG_EPN_CONTROL,
					USBF_EPN_DW_MASK,
					USBF_EPN_DEND);
	}

	dev_dbg(epn->udc->dev, "ep%u send %u/%u\n", epn->id, req->req.actual,
		req->req.length);

	return -EINPROGRESS;
}

static void usbf_epn_enable_in_end_int(struct usbf_ep *epn)
{
	usbf_ep_reg_bitset(epn, USBF_REG_EPN_INT_ENA, USBF_EPN_IN_END_EN);
}

static int usbf_epn_dma_in(struct usbf_ep *epn, struct usbf_req *req)
{
	unsigned int left;
	u32 npkt;
	u32 lastpkt;
	int ret;

	if (!IS_ALIGNED((uintptr_t)req->req.buf, 4)) {
		dev_dbg(epn->udc->dev, "ep%u buf unaligned -> fallback pio\n",
			epn->id);
		return usbf_epn_pio_in(epn, req);
	}

	left = req->req.length - req->req.actual;

	switch (req->xfer_step) {
	default:
	case USBF_XFER_START:
		if (left == 0) {
			dev_dbg(epn->udc->dev, "ep%u send null\n", epn->id);
			usbf_epn_send_null(epn);
			req->xfer_step = USBF_XFER_WAIT_END;
			break;
		}
		if (left < 4) {
			dev_dbg(epn->udc->dev, "ep%u send residue %u\n", epn->id,
				left);
			usbf_epn_send_residue(epn,
				req->req.buf + req->req.actual, left);
			req->req.actual += left;
			req->xfer_step = USBF_XFER_WAIT_END;
			break;
		}

		ret = usb_gadget_map_request(&epn->udc->gadget, &req->req, 1);
		if (ret < 0) {
			dev_err(epn->udc->dev, "usb_gadget_map_request failed (%d)\n",
				ret);
			return ret;
		}
		req->is_mapped = 1;

		npkt = DIV_ROUND_UP(left, epn->ep.maxpacket);
		lastpkt = (left % epn->ep.maxpacket);
		if (lastpkt == 0)
			lastpkt = epn->ep.maxpacket;
		lastpkt &= ~0x3; /* DMA is done on 32bit units */

		usbf_ep_dma_reg_writel(epn, USBF_REG_DMA_EPN_DCR2,
			USBF_SYS_EPN_MPKT(epn->ep.maxpacket) | USBF_SYS_EPN_LMPKT(lastpkt));
		usbf_ep_dma_reg_writel(epn, USBF_REG_DMA_EPN_TADR,
			req->req.dma);
		usbf_ep_dma_reg_writel(epn, USBF_REG_DMA_EPN_DCR1,
			USBF_SYS_EPN_SET_DMACNT(npkt));
		usbf_ep_dma_reg_bitset(epn, USBF_REG_DMA_EPN_DCR1,
			USBF_SYS_EPN_REQEN);

		usbf_ep_reg_writel(epn, USBF_REG_EPN_LEN_DCNT, USBF_EPN_SET_DMACNT(npkt));

		usbf_ep_reg_bitset(epn, USBF_REG_EPN_CONTROL, USBF_EPN_AUTO);

		/* The end of DMA transfer at the USBF level needs to be handle
		 * after the detection of the end of DMA transfer at the brige
		 * level.
		 * To force this sequence, EPN_IN_END_EN will be set by the
		 * detection of the end of transfer at bridge level (ie. bridge
		 * interrupt).
		 */
		usbf_ep_reg_bitclr(epn, USBF_REG_EPN_INT_ENA,
			USBF_EPN_IN_EN | USBF_EPN_IN_END_EN);
		epn->bridge_on_dma_end = usbf_epn_enable_in_end_int;

		/* Clear any pending IN_END interrupt */
		usbf_ep_reg_writel(epn, USBF_REG_EPN_STATUS, ~(u32)USBF_EPN_IN_END_INT);

		usbf_ep_reg_writel(epn, USBF_REG_EPN_DMA_CTRL,
			USBF_EPN_BURST_SET | USBF_EPN_DMAMODE0);
		usbf_ep_reg_bitset(epn, USBF_REG_EPN_DMA_CTRL,
			USBF_EPN_DMA_EN);

		req->dma_size = (npkt - 1) * epn->ep.maxpacket + lastpkt;

		dev_dbg(epn->udc->dev, "ep%u dma xfer %zu\n", epn->id,
			req->dma_size);

		req->xfer_step = USBF_XFER_WAIT_DMA;
		break;

	case USBF_XFER_WAIT_DMA:
		if (!(epn->status & USBF_EPN_IN_END_INT)) {
			dev_dbg(epn->udc->dev, "ep%u dma not done\n", epn->id);
			break;
		}
		dev_dbg(epn->udc->dev, "ep%u dma done\n", epn->id);

		usb_gadget_unmap_request(&epn->udc->gadget, &req->req, 1);
		req->is_mapped = 0;

		usbf_ep_reg_bitclr(epn, USBF_REG_EPN_CONTROL, USBF_EPN_AUTO);

		usbf_ep_reg_clrset(epn, USBF_REG_EPN_INT_ENA,
			USBF_EPN_IN_END_EN,
			USBF_EPN_IN_EN);

		req->req.actual += req->dma_size;

		left = req->req.length - req->req.actual;
		if (left) {
			usbf_ep_reg_writel(epn, USBF_REG_EPN_STATUS, ~(u32)USBF_EPN_IN_INT);

			dev_dbg(epn->udc->dev, "ep%u send residue %u\n", epn->id,
				left);
			usbf_epn_send_residue(epn,
				req->req.buf + req->req.actual, left);
			req->req.actual += left;
			req->xfer_step = USBF_XFER_WAIT_END;
			break;
		}

		if (req->req.actual % epn->ep.maxpacket) {
			/* last packet was a short packet. Tell the hardware to
			 * send it right now.
			 */
			dev_dbg(epn->udc->dev, "ep%u send short\n", epn->id);
			usbf_ep_reg_writel(epn, USBF_REG_EPN_STATUS,
				~(u32)USBF_EPN_IN_INT);
			usbf_ep_reg_bitset(epn, USBF_REG_EPN_CONTROL,
				USBF_EPN_DEND);

			req->xfer_step = USBF_XFER_WAIT_END;
			break;
		}

		/* Last packet size was a maxpacket size
		 * Send null packet if needed
		 */
		if (req->req.zero) {
			req->xfer_step = USBF_XFER_SEND_NULL;
			break;
		}

		/* No more action to do. Wait for the end of the USB transfer */
		req->xfer_step = USBF_XFER_WAIT_END;
		break;

	case USBF_XFER_SEND_NULL:
		dev_dbg(epn->udc->dev, "ep%u send null\n", epn->id);
		usbf_epn_send_null(epn);
		req->xfer_step = USBF_XFER_WAIT_END;
		break;

	case USBF_XFER_WAIT_END:
		if (!(epn->status & USBF_EPN_IN_INT)) {
			dev_dbg(epn->udc->dev, "ep%u end not done\n", epn->id);
			break;
		}
		dev_dbg(epn->udc->dev, "ep%u send done %u/%u\n", epn->id,
			req->req.actual, req->req.length);
		req->xfer_step = USBF_XFER_START;
		return 0;
	}

	return -EINPROGRESS;
}

static void usbf_epn_recv_residue(struct usbf_ep *epn, void *buf,
				  unsigned int size)
{
	u32 last;

	last = usbf_ep_reg_readl(epn, USBF_REG_EPN_READ);
	memcpy(buf, &last, size);
}

static int usbf_epn_pio_out(struct usbf_ep *epn, struct usbf_req *req)
{
	int req_status = 0;
	unsigned int count;
	unsigned int recv;
	unsigned int left;
	unsigned int nb;
	void *buf;

	if (epn->status & USBF_EPN_OUT_INT) {
		recv = USBF_EPN_GET_LDATA(
			usbf_ep_reg_readl(epn, USBF_REG_EPN_LEN_DCNT));
		count = recv;

		buf = req->req.buf;
		buf += req->req.actual;

		left = req->req.length - req->req.actual;

		dev_dbg(epn->udc->dev, "ep%u recv %u, left %u, mpkt %u\n", epn->id,
			recv, left, epn->ep.maxpacket);

		if (left > epn->ep.maxpacket)
			left = epn->ep.maxpacket;

		if (count > left) {
			req_status = -EOVERFLOW;
			count = left;
		}

		if (count) {
			nb = count / sizeof(u32);
			if (nb) {
				usbf_ep_reg_read_rep(epn, USBF_REG_EPN_READ,
					buf, nb);
				buf += (nb * sizeof(u32));
				req->req.actual += (nb * sizeof(u32));
				count -= (nb * sizeof(u32));
			}
			if (count) {
				usbf_epn_recv_residue(epn, buf, count);
				req->req.actual += count;
			}
		}
		dev_dbg(epn->udc->dev, "ep%u recv %u/%u\n", epn->id,
			req->req.actual, req->req.length);

		if (req_status) {
			dev_dbg(epn->udc->dev, "ep%u req.status=%d\n", epn->id,
				req_status);
			req->req.status = req_status;
			return 0;
		}

		if (recv < epn->ep.maxpacket) {
			dev_dbg(epn->udc->dev, "ep%u short packet\n", epn->id);
			/* This is a short packet -> It is the end */
			req->req.status = 0;
			return 0;
		}

		/* Request full -> complete */
		if (req->req.actual == req->req.length) {
			req->req.status = 0;
			return 0;
		}
	}

	if (epn->status & USBF_EPN_OUT_NULL_INT) {
		/* NULL packet received */
		dev_dbg(epn->udc->dev, "ep%u null packet\n", epn->id);
		if (req->req.actual != req->req.length) {
			req->req.status = req->req.short_not_ok ?
					  -EREMOTEIO : 0;
		} else {
			req->req.status = 0;
		}
		return 0;
	}

	return -EINPROGRESS;
}

static void usbf_epn_enable_out_end_int(struct usbf_ep *epn)
{
	usbf_ep_reg_bitset(epn, USBF_REG_EPN_INT_ENA, USBF_EPN_OUT_END_EN);
}

static void usbf_epn_process_queue(struct usbf_ep *epn);

static void usbf_epn_dma_out_send_dma(struct usbf_ep *epn, dma_addr_t addr, u32 npkt, bool is_short)
{
	usbf_ep_dma_reg_writel(epn, USBF_REG_DMA_EPN_DCR2, USBF_SYS_EPN_MPKT(epn->ep.maxpacket));
	usbf_ep_dma_reg_writel(epn, USBF_REG_DMA_EPN_TADR, addr);

	if (is_short) {
		usbf_ep_dma_reg_writel(epn, USBF_REG_DMA_EPN_DCR1,
				USBF_SYS_EPN_SET_DMACNT(1) | USBF_SYS_EPN_DIR0);
		usbf_ep_dma_reg_bitset(epn, USBF_REG_DMA_EPN_DCR1,
				USBF_SYS_EPN_REQEN);

		usbf_ep_reg_writel(epn, USBF_REG_EPN_LEN_DCNT,
				USBF_EPN_SET_DMACNT(0));

		/* The end of DMA transfer at the USBF level needs to be handled
		 * after the detection of the end of DMA transfer at the brige
		 * level.
		 * To force this sequence, enabling the OUT_END interrupt will
		 * be donee by the detection of the end of transfer at bridge
		 * level (ie. bridge interrupt).
		 */
		usbf_ep_reg_bitclr(epn, USBF_REG_EPN_INT_ENA,
			USBF_EPN_OUT_EN | USBF_EPN_OUT_NULL_EN | USBF_EPN_OUT_END_EN);
		epn->bridge_on_dma_end = usbf_epn_enable_out_end_int;

		/* Clear any pending OUT_END interrupt */
		usbf_ep_reg_writel(epn, USBF_REG_EPN_STATUS,
			~(u32)USBF_EPN_OUT_END_INT);

		usbf_ep_reg_writel(epn, USBF_REG_EPN_DMA_CTRL,
			USBF_EPN_STOP_MODE | USBF_EPN_STOP_SET | USBF_EPN_DMAMODE0);
		usbf_ep_reg_bitset(epn, USBF_REG_EPN_DMA_CTRL,
			USBF_EPN_DMA_EN);
		return;
	}

	usbf_ep_dma_reg_writel(epn, USBF_REG_DMA_EPN_DCR1,
		USBF_SYS_EPN_SET_DMACNT(npkt) | USBF_SYS_EPN_DIR0);
	usbf_ep_dma_reg_bitset(epn, USBF_REG_DMA_EPN_DCR1,
		USBF_SYS_EPN_REQEN);

	usbf_ep_reg_writel(epn, USBF_REG_EPN_LEN_DCNT,
		USBF_EPN_SET_DMACNT(npkt));

	/* Here, the bridge may or may not generate an interrupt to signal the
	 * end of DMA transfer.
	 * Keep only OUT_END interrupt and let handle the bridge later during
	 * the OUT_END processing.
	 */
	usbf_ep_reg_clrset(epn, USBF_REG_EPN_INT_ENA,
		USBF_EPN_OUT_EN | USBF_EPN_OUT_NULL_EN,
		USBF_EPN_OUT_END_EN);

	/* Disable bridge interrupt. It will be renabled later */
	usbf_reg_bitclr(epn->udc, USBF_REG_AHBBINTEN,
		USBF_SYS_DMA_ENDINTEN_EPN(epn->id));

	/* Clear any pending DMA_END interrupt at bridge level */
	usbf_reg_writel(epn->udc, USBF_REG_AHBBINT,
		USBF_SYS_DMA_ENDINT_EPN(epn->id));

	/* Clear any pending OUT_END interrupt */
	usbf_ep_reg_writel(epn, USBF_REG_EPN_STATUS,
		~(u32)USBF_EPN_OUT_END_INT);

	usbf_ep_reg_writel(epn, USBF_REG_EPN_DMA_CTRL,
		USBF_EPN_STOP_MODE | USBF_EPN_STOP_SET | USBF_EPN_DMAMODE0 | USBF_EPN_BURST_SET);
	usbf_ep_reg_bitset(epn, USBF_REG_EPN_DMA_CTRL,
		USBF_EPN_DMA_EN);
}

static size_t usbf_epn_dma_out_complete_dma(struct usbf_ep *epn, bool is_short)
{
	u32 dmacnt;
	u32 tmp;
	int ret;

	/* Restore interrupt mask */
	usbf_ep_reg_clrset(epn, USBF_REG_EPN_INT_ENA,
		USBF_EPN_OUT_END_EN,
		USBF_EPN_OUT_EN | USBF_EPN_OUT_NULL_EN);

	if (is_short) {
		/* Nothing more to do when the DMA was for a short packet */
		return 0;
	}

	/* Enable the bridge interrupt */
	usbf_reg_bitset(epn->udc, USBF_REG_AHBBINTEN,
		USBF_SYS_DMA_ENDINTEN_EPN(epn->id));

	tmp = usbf_ep_reg_readl(epn, USBF_REG_EPN_LEN_DCNT);
	dmacnt = USBF_EPN_GET_DMACNT(tmp);

	if (dmacnt) {
		/* Some packet were not received (halted by a short or a null
		 * packet.
		 * The bridge never raises an interrupt in this case.
		 * Wait for the end of transfer at bridge level
		 */
		ret = readl_poll_timeout_atomic(
			epn->dma_regs + USBF_REG_DMA_EPN_DCR1,
			tmp, (USBF_SYS_EPN_GET_DMACNT(tmp) == dmacnt),
			0,  10000);
		if (ret) {
			dev_err(epn->udc->dev, "ep%u wait bridge timed out\n",
				epn->id);
		}

		usbf_ep_dma_reg_bitclr(epn, USBF_REG_DMA_EPN_DCR1,
			USBF_SYS_EPN_REQEN);

		/* The dmacnt value tells how many packet were not transferred
		 * from the maximum number of packet we set for the DMA transfer.
		 * Compute the left DMA size based on this value.
		 */
		return dmacnt * epn->ep.maxpacket;
	}

	return 0;
}

static int usbf_epn_dma_out(struct usbf_ep *epn, struct usbf_req *req)
{
	unsigned int dma_left;
	unsigned int count;
	unsigned int recv;
	unsigned int left;
	u32 npkt;
	int ret;

	if (!IS_ALIGNED((uintptr_t)req->req.buf, 4)) {
		dev_dbg(epn->udc->dev, "ep%u buf unaligned -> fallback pio\n",
			epn->id);
		return usbf_epn_pio_out(epn, req);
	}

	switch (req->xfer_step) {
	default:
	case USBF_XFER_START:
		if (epn->status & USBF_EPN_OUT_NULL_INT) {
			dev_dbg(epn->udc->dev, "ep%u null packet\n", epn->id);
			if (req->req.actual != req->req.length) {
				req->req.status = req->req.short_not_ok ?
					-EREMOTEIO : 0;
			} else {
				req->req.status = 0;
			}
			return 0;
		}

		if (!(epn->status & USBF_EPN_OUT_INT)) {
			dev_dbg(epn->udc->dev, "ep%u OUT_INT not set -> spurious\n",
				epn->id);
			break;
		}

		recv = USBF_EPN_GET_LDATA(
			usbf_ep_reg_readl(epn, USBF_REG_EPN_LEN_DCNT));
		if (!recv) {
			dev_dbg(epn->udc->dev, "ep%u recv = 0 -> spurious\n",
				epn->id);
			break;
		}

		left = req->req.length - req->req.actual;

		dev_dbg(epn->udc->dev, "ep%u recv %u, left %u, mpkt %u\n", epn->id,
			recv, left, epn->ep.maxpacket);

		if (recv > left) {
			dev_err(epn->udc->dev, "ep%u overflow (%u/%u)\n",
				epn->id, recv, left);
			req->req.status = -EOVERFLOW;
			return -EOVERFLOW;
		}

		if (recv < epn->ep.maxpacket) {
			/* Short packet received */
			dev_dbg(epn->udc->dev, "ep%u short packet\n", epn->id);
			if (recv <= 3) {
				usbf_epn_recv_residue(epn,
					req->req.buf + req->req.actual, recv);
				req->req.actual += recv;

				dev_dbg(epn->udc->dev, "ep%u recv done %u/%u\n",
					epn->id, req->req.actual, req->req.length);

				req->xfer_step = USBF_XFER_START;
				return 0;
			}

			ret = usb_gadget_map_request(&epn->udc->gadget, &req->req, 0);
			if (ret < 0) {
				dev_err(epn->udc->dev, "map request failed (%d)\n",
					ret);
				return ret;
			}
			req->is_mapped = 1;

			usbf_epn_dma_out_send_dma(epn,
				req->req.dma + req->req.actual,
				1, true);
			req->dma_size = recv & ~0x3;

			dev_dbg(epn->udc->dev, "ep%u dma short xfer %zu\n", epn->id,
				req->dma_size);

			req->xfer_step = USBF_XFER_WAIT_DMA_SHORT;
			break;
		}

		ret = usb_gadget_map_request(&epn->udc->gadget, &req->req, 0);
		if (ret < 0) {
			dev_err(epn->udc->dev, "map request failed (%d)\n",
				ret);
			return ret;
		}
		req->is_mapped = 1;

		/* Use the maximum DMA size according to the request buffer.
		 * We will adjust the received size later at the end of the DMA
		 * transfer with the left size computed from
		 * usbf_epn_dma_out_complete_dma().
		 */
		npkt = left / epn->ep.maxpacket;
		usbf_epn_dma_out_send_dma(epn,
				req->req.dma + req->req.actual,
				npkt, false);
		req->dma_size = npkt * epn->ep.maxpacket;

		dev_dbg(epn->udc->dev, "ep%u dma xfer %zu (%u)\n", epn->id,
			req->dma_size, npkt);

		req->xfer_step = USBF_XFER_WAIT_DMA;
		break;

	case USBF_XFER_WAIT_DMA_SHORT:
		if (!(epn->status & USBF_EPN_OUT_END_INT)) {
			dev_dbg(epn->udc->dev, "ep%u dma short not done\n", epn->id);
			break;
		}
		dev_dbg(epn->udc->dev, "ep%u dma short done\n", epn->id);

		usbf_epn_dma_out_complete_dma(epn, true);

		usb_gadget_unmap_request(&epn->udc->gadget, &req->req, 0);
		req->is_mapped = 0;

		req->req.actual += req->dma_size;

		recv = USBF_EPN_GET_LDATA(
			usbf_ep_reg_readl(epn, USBF_REG_EPN_LEN_DCNT));

		count = recv & 0x3;
		if (count) {
			dev_dbg(epn->udc->dev, "ep%u recv residue %u\n", epn->id,
				count);
			usbf_epn_recv_residue(epn,
				req->req.buf + req->req.actual, count);
			req->req.actual += count;
		}

		dev_dbg(epn->udc->dev, "ep%u recv done %u/%u\n", epn->id,
			req->req.actual, req->req.length);

		req->xfer_step = USBF_XFER_START;
		return 0;

	case USBF_XFER_WAIT_DMA:
		if (!(epn->status & USBF_EPN_OUT_END_INT)) {
			dev_dbg(epn->udc->dev, "ep%u dma not done\n", epn->id);
			break;
		}
		dev_dbg(epn->udc->dev, "ep%u dma done\n", epn->id);

		dma_left = usbf_epn_dma_out_complete_dma(epn, false);
		if (dma_left) {
			/* Adjust the final DMA size with */
			count = req->dma_size - dma_left;

			dev_dbg(epn->udc->dev, "ep%u dma xfer done %u\n", epn->id,
				count);

			req->req.actual += count;

			if (epn->status & USBF_EPN_OUT_NULL_INT) {
				/* DMA was stopped by a null packet reception */
				dev_dbg(epn->udc->dev, "ep%u dma stopped by null pckt\n",
					epn->id);
				usb_gadget_unmap_request(&epn->udc->gadget,
							 &req->req, 0);
				req->is_mapped = 0;

				usbf_ep_reg_writel(epn, USBF_REG_EPN_STATUS,
					~(u32)USBF_EPN_OUT_NULL_INT);

				if (req->req.actual != req->req.length) {
					req->req.status = req->req.short_not_ok ?
						  -EREMOTEIO : 0;
				} else {
					req->req.status = 0;
				}
				dev_dbg(epn->udc->dev, "ep%u recv done %u/%u\n",
					epn->id, req->req.actual, req->req.length);
				req->xfer_step = USBF_XFER_START;
				return 0;
			}

			recv = USBF_EPN_GET_LDATA(
				usbf_ep_reg_readl(epn, USBF_REG_EPN_LEN_DCNT));
			left = req->req.length - req->req.actual;
			if (recv > left) {
				dev_err(epn->udc->dev,
					"ep%u overflow (%u/%u)\n", epn->id,
					recv, left);
				req->req.status = -EOVERFLOW;
				usb_gadget_unmap_request(&epn->udc->gadget,
							 &req->req, 0);
				req->is_mapped = 0;

				req->xfer_step = USBF_XFER_START;
				return -EOVERFLOW;
			}

			if (recv > 3) {
				usbf_epn_dma_out_send_dma(epn,
					req->req.dma + req->req.actual,
					1, true);
				req->dma_size = recv & ~0x3;

				dev_dbg(epn->udc->dev, "ep%u dma short xfer %zu\n",
					epn->id, req->dma_size);

				req->xfer_step = USBF_XFER_WAIT_DMA_SHORT;
				break;
			}

			usb_gadget_unmap_request(&epn->udc->gadget, &req->req, 0);
			req->is_mapped = 0;

			count = recv & 0x3;
			if (count) {
				dev_dbg(epn->udc->dev, "ep%u recv residue %u\n",
					epn->id, count);
				usbf_epn_recv_residue(epn,
					req->req.buf + req->req.actual, count);
				req->req.actual += count;
			}

			dev_dbg(epn->udc->dev, "ep%u recv done %u/%u\n", epn->id,
				req->req.actual, req->req.length);

			req->xfer_step = USBF_XFER_START;
			return 0;
		}

		/* Process queue at bridge interrupt only */
		usbf_ep_reg_bitclr(epn, USBF_REG_EPN_INT_ENA,
			USBF_EPN_OUT_END_EN | USBF_EPN_OUT_EN | USBF_EPN_OUT_NULL_EN);
		epn->status = 0;
		epn->bridge_on_dma_end = usbf_epn_process_queue;

		req->xfer_step = USBF_XFER_WAIT_BRIDGE;
		break;

	case USBF_XFER_WAIT_BRIDGE:
		dev_dbg(epn->udc->dev, "ep%u bridge transfers done\n", epn->id);

		/* Restore interrupt mask */
		usbf_ep_reg_clrset(epn, USBF_REG_EPN_INT_ENA,
			USBF_EPN_OUT_END_EN,
			USBF_EPN_OUT_EN | USBF_EPN_OUT_NULL_EN);

		usb_gadget_unmap_request(&epn->udc->gadget, &req->req, 0);
		req->is_mapped = 0;

		req->req.actual += req->dma_size;

		req->xfer_step = USBF_XFER_START;
		left = req->req.length - req->req.actual;
		if (!left) {
			/* No more data can be added to the buffer */
			dev_dbg(epn->udc->dev, "ep%u recv done %u/%u\n", epn->id,
				req->req.actual, req->req.length);
			return 0;
		}
		dev_dbg(epn->udc->dev, "ep%u recv done %u/%u, wait more data\n",
			epn->id, req->req.actual, req->req.length);
		break;
	}

	return -EINPROGRESS;
}

static void usbf_epn_dma_stop(struct usbf_ep *epn)
{
	usbf_ep_dma_reg_bitclr(epn, USBF_REG_DMA_EPN_DCR1, USBF_SYS_EPN_REQEN);

	/* In the datasheet:
	 *   If EP[m]_REQEN = 0b is set during DMA transfer, AHB-EPC stops DMA
	 *   after 1 packet transfer completed.
	 *   Therefore, wait sufficient time for ensuring DMA transfer
	 *   completion. The WAIT time depends on the system, especially AHB
	 *   bus activity
	 * So arbitrary 10ms would be sufficient.
	 */
	mdelay(10);

	usbf_ep_reg_bitclr(epn, USBF_REG_EPN_DMA_CTRL, USBF_EPN_DMA_EN);
}

static void usbf_epn_dma_abort(struct usbf_ep *epn,  struct usbf_req *req)
{
	dev_dbg(epn->udc->dev, "ep%u %s dma abort\n", epn->id,
		epn->is_in ? "in" : "out");

	epn->bridge_on_dma_end = NULL;

	usbf_epn_dma_stop(epn);

	usb_gadget_unmap_request(&epn->udc->gadget, &req->req,
				 epn->is_in ? 1 : 0);
	req->is_mapped = 0;

	usbf_ep_reg_bitclr(epn, USBF_REG_EPN_CONTROL, USBF_EPN_AUTO);

	if (epn->is_in) {
		usbf_ep_reg_clrset(epn, USBF_REG_EPN_INT_ENA,
			USBF_EPN_IN_END_EN,
			USBF_EPN_IN_EN);
	} else {
		usbf_ep_reg_clrset(epn, USBF_REG_EPN_INT_ENA,
			USBF_EPN_OUT_END_EN,
			USBF_EPN_OUT_EN | USBF_EPN_OUT_NULL_EN);
	}

	/* As dma is stopped, be sure that no DMA interrupt are pending */
	usbf_ep_reg_writel(epn, USBF_REG_EPN_STATUS,
		USBF_EPN_IN_END_INT | USBF_EPN_OUT_END_INT);

	usbf_reg_writel(epn->udc, USBF_REG_AHBBINT, USBF_SYS_DMA_ENDINT_EPN(epn->id));

	/* Enable DMA interrupt the bridge level */
	usbf_reg_bitset(epn->udc, USBF_REG_AHBBINTEN,
		USBF_SYS_DMA_ENDINTEN_EPN(epn->id));

	/* Reset transfer step */
	req->xfer_step = USBF_XFER_START;
}

static void usbf_epn_fifo_flush(struct usbf_ep *epn)
{
	u32 ctrl;
	u32 sts;
	int ret;

	dev_dbg(epn->udc->dev, "ep%u %s fifo flush\n", epn->id,
		epn->is_in ? "in" : "out");

	ctrl = usbf_ep_reg_readl(epn, USBF_REG_EPN_CONTROL);
	usbf_ep_reg_writel(epn, USBF_REG_EPN_CONTROL, ctrl | USBF_EPN_BCLR);

	if (ctrl & USBF_EPN_DIR0)
		return;

	ret = readl_poll_timeout_atomic(epn->regs + USBF_REG_EPN_STATUS, sts,
		(sts & (USBF_EPN_IN_DATA | USBF_EPN_IN_EMPTY)) == USBF_EPN_IN_EMPTY,
		0,  10000);
	if (ret)
		dev_err(epn->udc->dev, "ep%u flush fifo timed out\n", epn->id);
}

static void usbf_ep_req_done(struct usbf_ep *ep, struct usbf_req *req,
			     int status)
{
	list_del_init(&req->queue);

	if (status) {
		req->req.status = status;
	} else {
		if (req->req.status == -EINPROGRESS)
			req->req.status = status;
	}

	dev_dbg(ep->udc->dev, "ep%u %s req done length %u/%u, status=%d\n", ep->id,
		ep->is_in ? "in" : "out",
		req->req.actual, req->req.length, req->req.status);

	if (req->is_mapped)
		usbf_epn_dma_abort(ep, req);

	spin_unlock(&ep->udc->lock);
	usb_gadget_giveback_request(&ep->ep, &req->req);
	spin_lock(&ep->udc->lock);
}

static void usbf_ep_nuke(struct usbf_ep *ep, int status)
{
	struct usbf_req *req;

	dev_dbg(ep->udc->dev, "ep%u %s nuke status %d\n", ep->id,
		ep->is_in ? "in" : "out",
		status);

	while (!list_empty(&ep->queue)) {
		req = list_first_entry(&ep->queue, struct usbf_req, queue);
		usbf_ep_req_done(ep, req, status);
	}

	if (ep->id == 0)
		usbf_ep0_fifo_flush(ep);
	else
		usbf_epn_fifo_flush(ep);
}

static bool usbf_ep_is_stalled(struct usbf_ep *ep)
{
	u32 ctrl;

	if (ep->id == 0) {
		ctrl = usbf_ep_reg_readl(ep, USBF_REG_EP0_CONTROL);
		return (ctrl & USBF_EP0_STL) ? true : false;
	}

	ctrl = usbf_ep_reg_readl(ep, USBF_REG_EPN_CONTROL);
	if (ep->is_in)
		return (ctrl & USBF_EPN_ISTL) ? true : false;

	return (ctrl & USBF_EPN_OSTL) ? true : false;
}

static int usbf_epn_start_queue(struct usbf_ep *epn)
{
	struct usbf_req *req;
	int ret;

	if (usbf_ep_is_stalled(epn))
		return 0;

	req = list_first_entry_or_null(&epn->queue, struct usbf_req, queue);

	if (epn->is_in) {
		if (req && !epn->is_processing) {
			ret = epn->dma_regs ?
				usbf_epn_dma_in(epn, req) :
				usbf_epn_pio_in(epn, req);
			if (ret != -EINPROGRESS) {
				dev_err(epn->udc->dev,
					"queued next request not in progress\n");
					/* The request cannot be completed (ie
					 * ret == 0) on the first call.
					 * stall and nuke the endpoint
					 */
				return ret ? ret : -EIO;
			}
		}
	} else {
		if (req) {
			/* Clear ONAK to accept OUT tokens */
			usbf_ep_reg_bitclr(epn, USBF_REG_EPN_CONTROL,
				USBF_EPN_ONAK);

			/* Enable interrupts */
			usbf_ep_reg_bitset(epn, USBF_REG_EPN_INT_ENA,
				USBF_EPN_OUT_INT | USBF_EPN_OUT_NULL_INT);
		} else {
			/* Disable incoming data and interrupt.
			 * They will be enable on next usb_eb_queue call
			 */
			usbf_ep_reg_bitset(epn, USBF_REG_EPN_CONTROL,
				USBF_EPN_ONAK);
			usbf_ep_reg_bitclr(epn, USBF_REG_EPN_INT_ENA,
				USBF_EPN_OUT_INT | USBF_EPN_OUT_NULL_INT);
		}
	}
	return 0;
}

static int usbf_ep_process_queue(struct usbf_ep *ep)
{
	int (*usbf_ep_xfer)(struct usbf_ep *ep, struct usbf_req *req);
	struct usbf_req *req;
	int is_processing;
	int ret;

	if (ep->is_in) {
		usbf_ep_xfer = usbf_ep0_pio_in;
		if (ep->id) {
			usbf_ep_xfer = ep->dma_regs ?
					usbf_epn_dma_in : usbf_epn_pio_in;
		}
	} else {
		usbf_ep_xfer = usbf_ep0_pio_out;
		if (ep->id) {
			usbf_ep_xfer = ep->dma_regs ?
					usbf_epn_dma_out : usbf_epn_pio_out;
		}
	}

	req = list_first_entry_or_null(&ep->queue, struct usbf_req, queue);
	if (!req) {
		dev_err(ep->udc->dev,
			"no request available for ep%u %s process\n", ep->id,
			ep->is_in ? "in" : "out");
		return -ENOENT;
	}

	do {
		/* Were going to read the FIFO for this current request.
		 * NAK any other incoming data to avoid a race condition if no
		 * more request are available.
		 */
		if (!ep->is_in && ep->id != 0) {
			usbf_ep_reg_bitset(ep, USBF_REG_EPN_CONTROL,
				USBF_EPN_ONAK);
		}

		ret = usbf_ep_xfer(ep, req);
		if (ret == -EINPROGRESS) {
			if (!ep->is_in && ep->id != 0) {
				/* The current request needs more data.
				 * Allow incoming data
				 */
				usbf_ep_reg_bitclr(ep, USBF_REG_EPN_CONTROL,
					USBF_EPN_ONAK);
			}
			return ret;
		}

		is_processing = ep->is_processing;
		ep->is_processing = 1;
		usbf_ep_req_done(ep, req, ret);
		ep->is_processing = is_processing;

		if (ret) {
			/* An error was detected during the request transfer.
			 * Any pending DMA transfers were aborted by the
			 * usbf_ep_req_done() call.
			 * It's time to flush the fifo
			 */
			if (ep->id == 0)
				usbf_ep0_fifo_flush(ep);
			else
				usbf_epn_fifo_flush(ep);
		}

		req = list_first_entry_or_null(&ep->queue, struct usbf_req,
					       queue);

		if (ep->is_in)
			continue;

		if (ep->id != 0) {
			if (req) {
				/* An other request is available.
				 * Allow incoming data
				 */
				usbf_ep_reg_bitclr(ep, USBF_REG_EPN_CONTROL,
					USBF_EPN_ONAK);
			} else {
				/* No request queued. Disable interrupts.
				 * They will be enabled on usb_ep_queue
				 */
				usbf_ep_reg_bitclr(ep, USBF_REG_EPN_INT_ENA,
					USBF_EPN_OUT_INT | USBF_EPN_OUT_NULL_INT);
			}
		}
		/* Do not recall usbf_ep_xfer() */
		return req ? -EINPROGRESS : 0;

	} while (req);

	return 0;
}

static void usbf_ep_stall(struct usbf_ep *ep, bool stall)
{
	struct usbf_req *first;

	dev_dbg(ep->udc->dev, "ep%u %s %s\n", ep->id,
		ep->is_in ? "in" : "out",
		stall ? "stall" : "unstall");

	if (ep->id == 0) {
		if (stall)
			usbf_ep_reg_bitset(ep, USBF_REG_EP0_CONTROL, USBF_EP0_STL);
		else
			usbf_ep_reg_bitclr(ep, USBF_REG_EP0_CONTROL, USBF_EP0_STL);
		return;
	}

	if (stall) {
		if (ep->is_in)
			usbf_ep_reg_bitset(ep, USBF_REG_EPN_CONTROL,
				USBF_EPN_ISTL);
		else
			usbf_ep_reg_bitset(ep, USBF_REG_EPN_CONTROL,
				USBF_EPN_OSTL | USBF_EPN_OSTL_EN);
	} else {
		first = list_first_entry_or_null(&ep->queue, struct usbf_req, queue);
		if (first && first->is_mapped) {
			/* This can appear if the host halts an endpoint using
			 * SET_FEATURE and then un-halts the endpoint
			 */
			usbf_epn_dma_abort(ep, first);
		}
		usbf_epn_fifo_flush(ep);
		if (ep->is_in) {
			usbf_ep_reg_clrset(ep, USBF_REG_EPN_CONTROL,
				USBF_EPN_ISTL,
				USBF_EPN_IPIDCLR);
		} else {
			usbf_ep_reg_clrset(ep, USBF_REG_EPN_CONTROL,
				USBF_EPN_OSTL,
				USBF_EPN_OSTL_EN | USBF_EPN_OPIDCLR);
		}
		usbf_epn_start_queue(ep);
	}
}

static void usbf_ep0_enable(struct usbf_ep *ep0)
{
	usbf_ep_reg_writel(ep0, USBF_REG_EP0_CONTROL, USBF_EP0_INAK_EN | USBF_EP0_BCLR);

	usbf_ep_reg_writel(ep0, USBF_REG_EP0_INT_ENA,
		USBF_EP0_SETUP_EN | USBF_EP0_STG_START_EN | USBF_EP0_STG_END_EN |
		USBF_EP0_OUT_EN | USBF_EP0_OUT_NULL_EN | USBF_EP0_IN_EN);

	ep0->udc->ep0state = EP0_IDLE;
	ep0->disabled = 0;

	/* enable interrupts for the ep0 */
	usbf_reg_bitset(ep0->udc, USBF_REG_USB_INT_ENA, USBF_USB_EPN_EN(0));
}

static int usbf_epn_enable(struct usbf_ep *epn)
{
	u32 base_addr;
	u32 ctrl;

	base_addr = usbf_ep_info[epn->id].base_addr;
	usbf_ep_reg_writel(epn, USBF_REG_EPN_PCKT_ADRS,
		USBF_EPN_BASEAD(base_addr) | USBF_EPN_MPKT(epn->ep.maxpacket));

	/* OUT transfer interrupt are enabled during usb_ep_queue */
	if (epn->is_in) {
		/* Will be changed in DMA processing */
		usbf_ep_reg_writel(epn, USBF_REG_EPN_INT_ENA, USBF_EPN_IN_EN);
	}

	/* Clear, set endpoint direction, set IN/OUT STL, and enable
	 * Send NAK for Data out as request are not queued yet
	 */
	ctrl = USBF_EPN_EN | USBF_EPN_BCLR;
	if (epn->is_in)
		ctrl |= USBF_EPN_OSTL | USBF_EPN_OSTL_EN;
	else
		ctrl |= USBF_EPN_DIR0 | USBF_EPN_ISTL | USBF_EPN_OSTL_EN | USBF_EPN_ONAK;
	usbf_ep_reg_writel(epn, USBF_REG_EPN_CONTROL, ctrl);

	return 0;
}

static int usbf_ep_enable(struct usb_ep *_ep,
			  const struct usb_endpoint_descriptor *desc)
{
	struct usbf_ep *ep = container_of(_ep, struct usbf_ep, ep);
	struct usbf_udc *udc = ep->udc;
	unsigned long flags;
	int ret;

	if (ep->id == 0)
		return -EINVAL;

	if (!desc || desc->bDescriptorType != USB_DT_ENDPOINT)
		return -EINVAL;

	dev_dbg(ep->udc->dev, "ep%u %s mpkts %d\n", ep->id,
		usb_endpoint_dir_in(desc) ? "in" : "out",
		usb_endpoint_maxp(desc));

	spin_lock_irqsave(&ep->udc->lock, flags);
	ep->is_in = usb_endpoint_dir_in(desc);
	ep->ep.maxpacket = usb_endpoint_maxp(desc);

	ret = usbf_epn_enable(ep);
	if (ret)
		goto end;

	ep->disabled = 0;

	/* enable interrupts for this endpoint */
	usbf_reg_bitset(udc, USBF_REG_USB_INT_ENA, USBF_USB_EPN_EN(ep->id));

	/* enable DMA interrupt at bridge level if DMA is used */
	if (ep->dma_regs) {
		ep->bridge_on_dma_end = NULL;
		usbf_reg_bitset(udc, USBF_REG_AHBBINTEN,
			USBF_SYS_DMA_ENDINTEN_EPN(ep->id));
	}

	ret = 0;
end:
	spin_unlock_irqrestore(&ep->udc->lock, flags);
	return ret;
}

static int usbf_epn_disable(struct usbf_ep *epn)
{
	/* Disable interrupts */
	usbf_ep_reg_writel(epn, USBF_REG_EPN_INT_ENA, 0);

	/* Disable endpoint */
	usbf_ep_reg_bitclr(epn, USBF_REG_EPN_CONTROL, USBF_EPN_EN);

	/* remove anything that was pending */
	usbf_ep_nuke(epn, -ESHUTDOWN);

	return 0;
}

static int usbf_ep_disable(struct usb_ep *_ep)
{
	struct usbf_ep *ep = container_of(_ep, struct usbf_ep, ep);
	struct usbf_udc *udc = ep->udc;
	unsigned long flags;
	int ret;

	if (ep->id == 0)
		return -EINVAL;

	dev_dbg(ep->udc->dev, "ep%u %s mpkts %d\n", ep->id,
		ep->is_in ? "in" : "out", ep->ep.maxpacket);

	spin_lock_irqsave(&ep->udc->lock, flags);
	ep->disabled = 1;
	/* Disable DMA interrupt */
	if (ep->dma_regs) {
		usbf_reg_bitclr(udc, USBF_REG_AHBBINTEN,
			USBF_SYS_DMA_ENDINTEN_EPN(ep->id));
		ep->bridge_on_dma_end = NULL;
	}
	/* disable interrupts for this endpoint */
	usbf_reg_bitclr(udc, USBF_REG_USB_INT_ENA, USBF_USB_EPN_EN(ep->id));
	/* and the endpoint itself */
	ret = usbf_epn_disable(ep);
	spin_unlock_irqrestore(&ep->udc->lock, flags);

	return ret;
}

static int usbf_ep0_queue(struct usbf_ep *ep0, struct usbf_req *req,
			  gfp_t gfp_flags)
{
	int ret;

	req->req.actual = 0;
	req->req.status = -EINPROGRESS;
	req->is_zero_sent = 0;

	list_add_tail(&req->queue, &ep0->queue);

	if (ep0->udc->ep0state == EP0_IN_STATUS_START_PHASE)
		return 0;

	if (!ep0->is_in)
		return 0;

	if (ep0->udc->ep0state == EP0_IN_STATUS_PHASE) {
		if (req->req.length) {
			dev_err(ep0->udc->dev,
				"request lng %u for ep0 in status phase\n",
				req->req.length);
			return -EINVAL;
		}
		ep0->delayed_status = 0;
	}
	if (!ep0->is_processing) {
		ret = usbf_ep0_pio_in(ep0, req);
		if (ret != -EINPROGRESS) {
			dev_err(ep0->udc->dev,
				"queued request not in progress\n");
			/* The request cannot be completed (ie
			 * ret == 0) on the first call
			 */
			return ret ? ret : -EIO;
		}
	}

	return 0;
}

static int usbf_epn_queue(struct usbf_ep *ep, struct usbf_req *req,
			  gfp_t gfp_flags)
{
	int was_empty;
	int ret;

	if (ep->disabled) {
		dev_err(ep->udc->dev, "ep%u request queue while disable\n",
			ep->id);
		return -ESHUTDOWN;
	}

	req->req.actual = 0;
	req->req.status = -EINPROGRESS;
	req->is_zero_sent = 0;
	req->xfer_step = USBF_XFER_START;

	was_empty = list_empty(&ep->queue);
	list_add_tail(&req->queue, &ep->queue);
	if (was_empty) {
		ret = usbf_epn_start_queue(ep);
		if (ret)
			return ret;
	}
	return 0;
}

static int usbf_ep_queue(struct usb_ep *_ep, struct usb_request *_req,
			 gfp_t gfp_flags)
{
	struct usbf_req *req = container_of(_req, struct usbf_req, req);
	struct usbf_ep *ep = container_of(_ep, struct usbf_ep, ep);
	struct usbf_udc *udc = ep->udc;
	unsigned long flags;
	int ret;

	if (!_req || !_req->buf)
		return -EINVAL;

	if (!udc || !udc->driver)
		return -EINVAL;

	dev_dbg(ep->udc->dev, "ep%u %s req queue length %u, zero %u, short_not_ok %u\n",
		ep->id, ep->is_in ? "in" : "out",
		req->req.length, req->req.zero, req->req.short_not_ok);

	spin_lock_irqsave(&ep->udc->lock, flags);
	if (ep->id == 0)
		ret = usbf_ep0_queue(ep, req, gfp_flags);
	else
		ret = usbf_epn_queue(ep, req, gfp_flags);
	spin_unlock_irqrestore(&ep->udc->lock, flags);
	return ret;
}

static int usbf_ep_dequeue(struct usb_ep *_ep, struct usb_request *_req)
{
	struct usbf_req *req = container_of(_req, struct usbf_req, req);
	struct usbf_ep *ep = container_of(_ep, struct usbf_ep, ep);
	unsigned long flags;
	int is_processing;
	int first;
	int ret;

	spin_lock_irqsave(&ep->udc->lock, flags);

	dev_dbg(ep->udc->dev, "ep%u %s req dequeue length %u/%u\n",
		ep->id, ep->is_in ? "in" : "out",
		req->req.actual, req->req.length);

	first = list_is_first(&req->queue, &ep->queue);

	/* Complete the request but avoid any operation that could be done
	 * if a new request is queued during the request completion
	 */
	is_processing = ep->is_processing;
	ep->is_processing = 1;
	usbf_ep_req_done(ep, req, -ECONNRESET);
	ep->is_processing = is_processing;

	if (first) {
		/* The first item in the list was dequeued.
		 * This item could already be submitted to the hardware.
		 * So, flush the fifo
		 */
		if (ep->id)
			usbf_epn_fifo_flush(ep);
		else
			usbf_ep0_fifo_flush(ep);
	}

	if (ep->id == 0) {
		/* We dequeue a request on ep0. On this endpoint, we can have
		 * 1 request related to the data stage and/or 1 request
		 * related to the status stage.
		 * We dequeue one of them and so the USB control transaction
		 * is no more coherent. The simple way to be consistent after
		 * dequeuing is to stall and nuke the endpoint and wait the
		 * next SETUP packet.
		 */
		usbf_ep_stall(ep, true);
		usbf_ep_nuke(ep, -ECONNRESET);
		ep->udc->ep0state = EP0_IDLE;
		goto end;
	}

	if (!first)
		goto end;

	ret = usbf_epn_start_queue(ep);
	if (ret) {
		usbf_ep_stall(ep, true);
		usbf_ep_nuke(ep, -EIO);
	}
end:
	spin_unlock_irqrestore(&ep->udc->lock, flags);
	return 0;
}

static struct usb_request *usbf_ep_alloc_request(struct usb_ep *_ep,
						 gfp_t gfp_flags)
{
	struct usbf_req *req;

	if (!_ep)
		return NULL;

	req = kzalloc(sizeof(*req), gfp_flags);
	if (!req)
		return NULL;

	INIT_LIST_HEAD(&req->queue);

	return &req->req;
}

static void usbf_ep_free_request(struct usb_ep *_ep, struct usb_request *_req)
{
	struct usbf_req *req;
	unsigned long flags;
	struct usbf_ep *ep;

	if (!_ep || !_req)
		return;

	req = container_of(_req, struct usbf_req, req);
	ep = container_of(_ep, struct usbf_ep, ep);

	spin_lock_irqsave(&ep->udc->lock, flags);
	list_del_init(&req->queue);
	spin_unlock_irqrestore(&ep->udc->lock, flags);
	kfree(req);
}

static int usbf_ep_set_halt(struct usb_ep *_ep, int halt)
{
	struct usbf_ep *ep = container_of(_ep, struct usbf_ep, ep);
	unsigned long flags;
	int ret;

	if (ep->id == 0)
		return -EINVAL;

	spin_lock_irqsave(&ep->udc->lock, flags);

	if (!list_empty(&ep->queue)) {
		ret = -EAGAIN;
		goto end;
	}

	usbf_ep_stall(ep, halt);
	if (!halt)
		ep->is_wedged = 0;

	ret = 0;
end:
	spin_unlock_irqrestore(&ep->udc->lock, flags);

	return ret;
}

static int usbf_ep_set_wedge(struct usb_ep *_ep)
{
	struct usbf_ep *ep = container_of(_ep, struct usbf_ep, ep);
	unsigned long flags;
	int ret;

	if (ep->id == 0)
		return -EINVAL;

	spin_lock_irqsave(&ep->udc->lock, flags);
	if (!list_empty(&ep->queue)) {
		ret = -EAGAIN;
		goto end;
	}
	usbf_ep_stall(ep, 1);
	ep->is_wedged = 1;

	ret = 0;
end:
	spin_unlock_irqrestore(&ep->udc->lock, flags);
	return ret;
}

static struct usb_ep_ops usbf_ep_ops = {
	.enable = usbf_ep_enable,
	.disable = usbf_ep_disable,
	.queue = usbf_ep_queue,
	.dequeue = usbf_ep_dequeue,
	.set_halt = usbf_ep_set_halt,
	.set_wedge = usbf_ep_set_wedge,
	.alloc_request = usbf_ep_alloc_request,
	.free_request = usbf_ep_free_request,
};

static void usbf_ep0_req_complete(struct usb_ep *_ep, struct usb_request *_req)
{
}

static void usbf_ep0_fill_req(struct usbf_ep *ep0, struct usbf_req *req,
			      void *buf, unsigned int length,
			      void (*complete)(struct usb_ep *_ep,
					       struct usb_request *_req))
{
	if (buf && length)
		memcpy(ep0->udc->ep0_buf, buf, length);

	req->req.buf = ep0->udc->ep0_buf;
	req->req.length = length;
	req->req.dma = 0;
	req->req.zero = true;
	req->req.complete = complete ? complete : usbf_ep0_req_complete;
	req->req.status = -EINPROGRESS;
	req->req.context = NULL;
	req->req.actual = 0;
}

static struct usbf_ep *usbf_get_ep_by_addr(struct usbf_udc *udc, u8 address)
{
	struct usbf_ep *ep;
	unsigned int i;

	if ((address & USB_ENDPOINT_NUMBER_MASK) == 0)
		return &udc->ep[0];

	for (i = 1; i < ARRAY_SIZE(udc->ep); i++) {
		ep = &udc->ep[i];

		if (!ep->ep.desc)
			continue;

		if (ep->ep.desc->bEndpointAddress == address)
			return ep;
	}

	return NULL;
}

static int usbf_req_delegate(struct usbf_udc *udc,
			     const struct usb_ctrlrequest *ctrlrequest)
{
	int ret;

	spin_unlock(&udc->lock);
	ret = udc->driver->setup(&udc->gadget, ctrlrequest);
	spin_lock(&udc->lock);
	if (ret < 0) {
		dev_dbg(udc->dev, "udc driver setup failed %d\n", ret);
		return ret;
	}
	if (ret == USB_GADGET_DELAYED_STATUS) {
		dev_dbg(udc->dev, "delayed status set\n");
		udc->ep[0].delayed_status = 1;
		return 0;
	}
	return ret;
}

static int usbf_req_get_status(struct usbf_udc *udc,
			       const struct usb_ctrlrequest *ctrlrequest)
{
	struct usbf_ep *ep;
	u16 status_data;
	u16 wLength;
	u16 wValue;
	u16 wIndex;

	wValue  = le16_to_cpu(ctrlrequest->wValue);
	wLength = le16_to_cpu(ctrlrequest->wLength);
	wIndex  = le16_to_cpu(ctrlrequest->wIndex);

	switch (ctrlrequest->bRequestType) {
	case USB_DIR_IN | USB_RECIP_DEVICE | USB_TYPE_STANDARD:
		if ((wValue != 0) || (wIndex != 0) || (wLength != 2))
			goto delegate;

		status_data = 0;
		if (udc->gadget.is_selfpowered)
			status_data |= BIT(USB_DEVICE_SELF_POWERED);

		if (udc->is_remote_wakeup)
			status_data |= BIT(USB_DEVICE_REMOTE_WAKEUP);

		break;

	case USB_DIR_IN | USB_RECIP_ENDPOINT | USB_TYPE_STANDARD:
		if ((wValue != 0) || (wLength != 2))
			goto delegate;

		ep = usbf_get_ep_by_addr(udc, wIndex);
		if (!ep)
			return -EINVAL;

		status_data = 0;
		if (usbf_ep_is_stalled(ep))
			status_data |= cpu_to_le16(1);
		break;

	case USB_DIR_IN | USB_RECIP_INTERFACE | USB_TYPE_STANDARD:
		if ((wValue != 0) || (wLength != 2))
			goto delegate;
		status_data = 0;
		break;

	default:
		goto delegate;
	}

	usbf_ep0_fill_req(&udc->ep[0], &udc->setup_reply, &status_data,
			  sizeof(status_data), NULL);
	usbf_ep0_queue(&udc->ep[0], &udc->setup_reply, GFP_ATOMIC);

	return 0;

delegate:
	return usbf_req_delegate(udc, ctrlrequest);
}

static int usbf_req_clear_set_feature(struct usbf_udc *udc,
				      const struct usb_ctrlrequest *ctrlrequest,
				      bool is_set)
{
	struct usbf_ep *ep;
	u16 wLength;
	u16 wValue;
	u16 wIndex;

	wValue  = le16_to_cpu(ctrlrequest->wValue);
	wLength = le16_to_cpu(ctrlrequest->wLength);
	wIndex  = le16_to_cpu(ctrlrequest->wIndex);

	switch (ctrlrequest->bRequestType) {
	case USB_DIR_OUT | USB_RECIP_DEVICE:
		if ((wIndex != 0) || (wLength != 0))
			goto delegate;

		if (wValue != cpu_to_le16(USB_DEVICE_REMOTE_WAKEUP))
			goto delegate;

		udc->is_remote_wakeup = is_set;
		break;

	case USB_DIR_OUT | USB_RECIP_ENDPOINT:
		if (wLength != 0)
			goto delegate;

		ep = usbf_get_ep_by_addr(udc, wIndex);
		if (!ep)
			return -EINVAL;

		if ((ep->id == 0) && is_set) {
			/* Endpoint 0 cannot be halted (stalled)
			 * Returning an error code leads to a STALL on this ep0
			 * but keep the automate in a consistent state.
			 */
			return -EINVAL;
		}
		if (ep->is_wedged && !is_set) {
			/* Ignore CLEAR_FEATURE(HALT ENDPOINT) when the
			 * endpoint is wedged
			 */
			break;
		}
		usbf_ep_stall(ep, is_set);
		break;

	default:
		goto delegate;
	}

	return 0;

delegate:
	return usbf_req_delegate(udc, ctrlrequest);
}

static void usbf_ep0_req_set_address_complete(struct usb_ep *_ep,
					      struct usb_request *_req)
{
	struct usbf_ep *ep = container_of(_ep, struct usbf_ep, ep);

	/* The status phase of the SET_ADDRESS request is completed ... */
	if (_req->status == 0) {
		/* ... without any errors -> Signaled the state to the core. */
		usb_gadget_set_state(&ep->udc->gadget, USB_STATE_ADDRESS);
	}

	/* In case of request failure, there is no need to revert the address
	 * value set to the hardware as the hardware will take care of the
	 * value only if the status stage is completed normally.
	 */
}

static int usbf_req_set_address(struct usbf_udc *udc,
				const struct usb_ctrlrequest *ctrlrequest)
{
	u16 wLength;
	u16 wValue;
	u16 wIndex;
	u32 addr;

	wValue  = le16_to_cpu(ctrlrequest->wValue);
	wLength = le16_to_cpu(ctrlrequest->wLength);
	wIndex  = le16_to_cpu(ctrlrequest->wIndex);

	if (ctrlrequest->bRequestType != (USB_DIR_OUT | USB_RECIP_DEVICE))
		goto delegate;

	if ((wIndex != 0) || (wLength != 0) || (wValue > 127))
		return -EINVAL;

	addr = wValue;
	/* The hardware will take care of this USB address after the status
	 * stage of the SET_ADDRESS request is completed normally.
	 * It is safe to write it now
	 */
	usbf_reg_writel(udc, USBF_REG_USB_ADDRESS, USBF_USB_SET_USB_ADDR(addr));

	/* Queued the status request */
	usbf_ep0_fill_req(&udc->ep[0], &udc->setup_reply, NULL, 0,
			  usbf_ep0_req_set_address_complete);
	usbf_ep0_queue(&udc->ep[0], &udc->setup_reply, GFP_ATOMIC);

	return 0;

delegate:
	return usbf_req_delegate(udc, ctrlrequest);
}

static int usbf_req_set_configuration(struct usbf_udc *udc,
				      const struct usb_ctrlrequest *ctrlrequest)
{
	u16 wLength;
	u16 wValue;
	u16 wIndex;
	int ret;

	ret = usbf_req_delegate(udc, ctrlrequest);
	if (ret)
		return ret;

	wValue  = le16_to_cpu(ctrlrequest->wValue);
	wLength = le16_to_cpu(ctrlrequest->wLength);
	wIndex  = le16_to_cpu(ctrlrequest->wIndex);

	if ((ctrlrequest->bRequestType != (USB_DIR_OUT | USB_RECIP_DEVICE)) ||
	    (wIndex != 0) || (wLength != 0)) {
		/* No error detected by driver->setup() but it is not an USB2.0
		 * Ch9 SET_CONFIGURATION.
		 * Nothing more to do
		 */
		return 0;
	}

	if (wValue & 0x00FF) {
		usbf_reg_bitset(udc, USBF_REG_USB_CONTROL, USBF_USB_CONF);
	} else {
		usbf_reg_bitclr(udc, USBF_REG_USB_CONTROL, USBF_USB_CONF);
		/* Go back to Address State */
		spin_unlock(&udc->lock);
		usb_gadget_set_state(&udc->gadget, USB_STATE_ADDRESS);
		spin_lock(&udc->lock);
	}

	return 0;
}

static int usbf_handle_ep0_setup(struct usbf_ep *ep0)
{
	union {
		struct usb_ctrlrequest ctrlreq;
		u32 raw[2];
	} crq;
	struct usbf_udc *udc = ep0->udc;
	int ret;

	/* Read setup data (ie the USB control request) */
	crq.raw[0] = usbf_reg_readl(udc, USBF_REG_SETUP_DATA0);
	crq.raw[1] = usbf_reg_readl(udc, USBF_REG_SETUP_DATA1);

	dev_dbg(ep0->udc->dev,
		"ep0 req%02x.%02x, wValue 0x%04x, wIndex 0x%04x, wLength 0x%04x\n",
		crq.ctrlreq.bRequestType, crq.ctrlreq.bRequest,
		crq.ctrlreq.wValue, crq.ctrlreq.wIndex, crq.ctrlreq.wLength);

	/* Set current EP0 state according to the received request */
	if (crq.ctrlreq.wLength) {
		if (crq.ctrlreq.bRequestType & USB_DIR_IN) {
			udc->ep0state = EP0_IN_DATA_PHASE;
			usbf_ep_reg_clrset(ep0, USBF_REG_EP0_CONTROL,
				USBF_EP0_INAK,
				USBF_EP0_INAK_EN);
			ep0->is_in = 1;
		} else {
			udc->ep0state = EP0_OUT_DATA_PHASE;
			usbf_ep_reg_bitclr(ep0, USBF_REG_EP0_CONTROL,
				USBF_EP0_ONAK);
			ep0->is_in = 0;
		}
	} else {
		udc->ep0state = EP0_IN_STATUS_START_PHASE;
		ep0->is_in = 1;
	}

	/* We starts a new control transfer -> Clear the delayed status flag */
	ep0->delayed_status = 0;

	if ((crq.ctrlreq.bRequestType & USB_TYPE_MASK) != USB_TYPE_STANDARD) {
		/* This is not a USB standard request -> delelate */
		goto delegate;
	}

	switch (crq.ctrlreq.bRequest) {
	case USB_REQ_GET_STATUS:
		ret = usbf_req_get_status(udc, &crq.ctrlreq);
		break;

	case USB_REQ_CLEAR_FEATURE:
		ret = usbf_req_clear_set_feature(udc, &crq.ctrlreq, false);
		break;

	case USB_REQ_SET_FEATURE:
		ret = usbf_req_clear_set_feature(udc, &crq.ctrlreq, true);
		break;

	case USB_REQ_SET_ADDRESS:
		ret = usbf_req_set_address(udc, &crq.ctrlreq);
		break;

	case USB_REQ_SET_CONFIGURATION:
		ret = usbf_req_set_configuration(udc, &crq.ctrlreq);
		break;

	default:
		goto delegate;
	}

	return ret;

delegate:
	return usbf_req_delegate(udc, &crq.ctrlreq);
}

static int usbf_handle_ep0_data_status(struct usbf_ep *ep0,
				  const char *ep0state_name,
				  enum usbf_ep0state next_ep0state)
{
	struct usbf_udc *udc = ep0->udc;
	int ret;

	ret = usbf_ep_process_queue(ep0);
	switch (ret) {
	case -ENOENT:
		dev_err(udc->dev,
			"no request available for ep0 %s phase\n",
			ep0state_name);
		break;
	case -EINPROGRESS:
		/* More data needs to be processed */
		ret = 0;
		break;
	case 0:
		/* All requests in the queue are processed */
		udc->ep0state = next_ep0state;
		break;
	default:
		dev_err(udc->dev,
			"process queue failed for ep0 %s phase (%d)\n",
			ep0state_name, ret);
		break;
	}
	return ret;
}

static int usbf_handle_ep0_out_status_start(struct usbf_ep *ep0)
{
	struct usbf_udc *udc = ep0->udc;
	struct usbf_req *req;

	usbf_ep_reg_clrset(ep0, USBF_REG_EP0_CONTROL,
				USBF_EP0_ONAK,
				USBF_EP0_PIDCLR);
	ep0->is_in = 0;

	req = list_first_entry_or_null(&ep0->queue, struct usbf_req, queue);
	if (!req) {
		usbf_ep0_fill_req(ep0, &udc->setup_reply, NULL, 0, NULL);
		usbf_ep0_queue(ep0, &udc->setup_reply, GFP_ATOMIC);
	} else {
		if (req->req.length) {
			dev_err(udc->dev,
				"queued request length %u for ep0 out status phase\n",
				req->req.length);
		}
	}
	udc->ep0state = EP0_OUT_STATUS_PHASE;
	return 0;
}

static int usbf_handle_ep0_in_status_start(struct usbf_ep *ep0)
{
	struct usbf_udc *udc = ep0->udc;
	struct usbf_req *req;
	int ret;

	usbf_ep_reg_clrset(ep0, USBF_REG_EP0_CONTROL,
				USBF_EP0_INAK,
				USBF_EP0_INAK_EN | USBF_EP0_PIDCLR);
	ep0->is_in = 1;

	/* Queue request for status if needed */
	req = list_first_entry_or_null(&ep0->queue, struct usbf_req, queue);
	if (!req) {
		if (ep0->delayed_status) {
			dev_dbg(ep0->udc->dev,
				"EP0_IN_STATUS_START_PHASE ep0->delayed_status set\n");
			udc->ep0state = EP0_IN_STATUS_PHASE;
			return 0;
		}

		usbf_ep0_fill_req(ep0, &udc->setup_reply, NULL,
			  0, NULL);
		usbf_ep0_queue(ep0, &udc->setup_reply,
			       GFP_ATOMIC);

		req = list_first_entry_or_null(&ep0->queue, struct usbf_req, queue);
	} else {
		if (req->req.length) {
			dev_err(udc->dev,
				"queued request length %u for ep0 in status phase\n",
				req->req.length);
		}
	}

	ret = usbf_ep0_pio_in(ep0, req);
	if (ret != -EINPROGRESS) {
		usbf_ep_req_done(ep0, req, ret);
		udc->ep0state = EP0_IN_STATUS_END_PHASE;
		return 0;
	}

	udc->ep0state = EP0_IN_STATUS_PHASE;
	return 0;
}

static void usbf_ep0_interrupt(struct usbf_ep *ep0)
{
	struct usbf_udc *udc = ep0->udc;
	u32 sts, prev_sts;
	int prev_ep0state;
	int ret;

	ep0->status = usbf_ep_reg_readl(ep0, USBF_REG_EP0_STATUS);
	usbf_ep_reg_writel(ep0, USBF_REG_EP0_STATUS, ~ep0->status);

	dev_dbg(ep0->udc->dev, "ep0 status=0x%08x, enable=%08x\n, ctrl=0x%08x\n",
		ep0->status,
		usbf_ep_reg_readl(ep0, USBF_REG_EP0_INT_ENA),
		usbf_ep_reg_readl(ep0, USBF_REG_EP0_CONTROL));

	sts = ep0->status & (USBF_EP0_SETUP_INT | USBF_EP0_IN_INT | USBF_EP0_OUT_INT |
			     USBF_EP0_OUT_NULL_INT | USBF_EP0_STG_START_INT |
			     USBF_EP0_STG_END_INT);

	ret = 0;
	do {
		dev_dbg(ep0->udc->dev, "udc->ep0state=%d\n", udc->ep0state);

		prev_sts = sts;
		prev_ep0state = udc->ep0state;
		switch (udc->ep0state) {
		case EP0_IDLE:
			if (!(sts & USBF_EP0_SETUP_INT))
				break;

			sts &= ~USBF_EP0_SETUP_INT;
			dev_dbg(ep0->udc->dev, "ep0 handle setup\n");
			ret = usbf_handle_ep0_setup(ep0);
			break;

		case EP0_IN_DATA_PHASE:
			if (!(sts & USBF_EP0_IN_INT))
				break;

			sts &= ~USBF_EP0_IN_INT;
			dev_dbg(ep0->udc->dev, "ep0 handle in data phase\n");
			ret = usbf_handle_ep0_data_status(ep0,
				"in data", EP0_OUT_STATUS_START_PHASE);
			break;

		case EP0_OUT_STATUS_START_PHASE:
			if (!(sts & USBF_EP0_STG_START_INT))
				break;

			sts &= ~USBF_EP0_STG_START_INT;
			dev_dbg(ep0->udc->dev, "ep0 handle out status start phase\n");
			ret = usbf_handle_ep0_out_status_start(ep0);
			break;

		case EP0_OUT_STATUS_PHASE:
			if (!(sts & (USBF_EP0_OUT_INT | USBF_EP0_OUT_NULL_INT)))
				break;

			sts &= ~(USBF_EP0_OUT_INT | USBF_EP0_OUT_NULL_INT);
			dev_dbg(ep0->udc->dev, "ep0 handle out status phase\n");
			ret = usbf_handle_ep0_data_status(ep0,
				"out status",
				EP0_OUT_STATUS_END_PHASE);
			break;

		case EP0_OUT_STATUS_END_PHASE:
			if (!(sts & (USBF_EP0_STG_END_INT | USBF_EP0_SETUP_INT)))
				break;

			sts &= ~USBF_EP0_STG_END_INT;
			dev_dbg(ep0->udc->dev, "ep0 handle out status end phase\n");
			udc->ep0state = EP0_IDLE;
			break;

		case EP0_OUT_DATA_PHASE:
			if (!(sts & (USBF_EP0_OUT_INT | USBF_EP0_OUT_NULL_INT)))
				break;

			sts &= ~(USBF_EP0_OUT_INT | USBF_EP0_OUT_NULL_INT);
			dev_dbg(ep0->udc->dev, "ep0 handle out data phase\n");
			ret = usbf_handle_ep0_data_status(ep0,
				"out data", EP0_IN_STATUS_START_PHASE);
			break;

		case EP0_IN_STATUS_START_PHASE:
			if (!(sts & USBF_EP0_STG_START_INT))
				break;

			sts &= ~USBF_EP0_STG_START_INT;
			dev_dbg(ep0->udc->dev, "ep0 handle in status start phase\n");
			ret = usbf_handle_ep0_in_status_start(ep0);
			break;

		case EP0_IN_STATUS_PHASE:
			if (!(sts & USBF_EP0_IN_INT))
				break;

			sts &= ~USBF_EP0_IN_INT;
			dev_dbg(ep0->udc->dev, "ep0 handle in status phase\n");
			ret = usbf_handle_ep0_data_status(ep0,
				"in status", EP0_IN_STATUS_END_PHASE);
			break;

		case EP0_IN_STATUS_END_PHASE:
			if (!(sts & (USBF_EP0_STG_END_INT | USBF_EP0_SETUP_INT)))
				break;

			sts &= ~USBF_EP0_STG_END_INT;
			dev_dbg(ep0->udc->dev, "ep0 handle in status end\n");
			udc->ep0state = EP0_IDLE;
			break;

		default:
			udc->ep0state = EP0_IDLE;
			break;
		}

		if (ret) {
			dev_dbg(ep0->udc->dev, "ep0 failed (%d)\n", ret);
			/* Failure -> stall.
			 * This stall state will be automatically cleared when
			 * the IP receives the next SETUP packet
			 */
			usbf_ep_stall(ep0, true);

			/* Remove anything that was pending */
			usbf_ep_nuke(ep0, -EPROTO);

			udc->ep0state = EP0_IDLE;
			break;
		}

	} while ((prev_ep0state != udc->ep0state) || (prev_sts != sts));

	dev_dbg(ep0->udc->dev, "ep0 done udc->ep0state=%d, status=0x%08x. next=0x%08x\n",
		udc->ep0state, sts,
		usbf_ep_reg_readl(ep0, USBF_REG_EP0_STATUS));
}

static void usbf_epn_process_queue(struct usbf_ep *epn)
{
	int ret;

	ret = usbf_ep_process_queue(epn);
	switch (ret) {
	case -ENOENT:
		dev_warn(epn->udc->dev, "ep%u %s, no request available\n",
			epn->id, epn->is_in ? "in" : "out");
		break;
	case -EINPROGRESS:
		/* More data needs to be processed */
		ret = 0;
		break;
	case 0:
		/* All requests in the queue are processed */
		break;
	default:
		dev_err(epn->udc->dev, "ep%u %s, process queue failed (%d)\n",
			epn->id, epn->is_in ? "in" : "out", ret);
		break;
	}

	if (ret) {
		dev_dbg(epn->udc->dev, "ep%u %s failed (%d)\n", epn->id,
			epn->is_in ? "in" : "out", ret);
		usbf_ep_stall(epn, true);
		usbf_ep_nuke(epn, ret);
	}
}

static void usbf_epn_interrupt(struct usbf_ep *epn)
{
	u32 sts;
	u32 ena;

	epn->status = usbf_ep_reg_readl(epn, USBF_REG_EPN_STATUS);
	ena = usbf_ep_reg_readl(epn, USBF_REG_EPN_INT_ENA);
	usbf_ep_reg_writel(epn, USBF_REG_EPN_STATUS, ~(epn->status & ena));

	dev_dbg(epn->udc->dev, "ep%u %s status=0x%08x, enable=%08x\n, ctrl=0x%08x\n",
		epn->id, epn->is_in ? "in" : "out", epn->status, ena,
		usbf_ep_reg_readl(epn, USBF_REG_EPN_CONTROL));

	if (epn->disabled) {
		dev_warn(epn->udc->dev, "ep%u %s, interrupt while disabled\n",
			epn->id, epn->is_in ? "in" : "out");
		return;
	}

	sts = epn->status & ena;

	if (sts & (USBF_EPN_IN_END_INT | USBF_EPN_IN_INT)) {
		sts &= ~(USBF_EPN_IN_END_INT | USBF_EPN_IN_INT);
		dev_dbg(epn->udc->dev, "ep%u %s process queue (in interrupts)\n",
			epn->id, epn->is_in ? "in" : "out");
		usbf_epn_process_queue(epn);
	}

	if (sts & (USBF_EPN_OUT_END_INT | USBF_EPN_OUT_INT | USBF_EPN_OUT_NULL_INT)) {
		sts &= ~(USBF_EPN_OUT_END_INT | USBF_EPN_OUT_INT | USBF_EPN_OUT_NULL_INT);
		dev_dbg(epn->udc->dev, "ep%u %s process queue (out interrupts)\n",
			epn->id, epn->is_in ? "in" : "out");
		usbf_epn_process_queue(epn);
	}

	dev_dbg(epn->udc->dev, "ep%u %s done status=0x%08x. next=0x%08x\n",
		epn->id, epn->is_in ? "in" : "out",
		sts, usbf_ep_reg_readl(epn, USBF_REG_EPN_STATUS));
}

static void usbf_ep_reset(struct usbf_ep *ep)
{
	ep->status = 0;
	/* Remove anything that was pending */
	usbf_ep_nuke(ep, -ESHUTDOWN);
}

static void usbf_reset(struct usbf_udc *udc)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(udc->ep); i++) {
		if (udc->ep[i].disabled)
			continue;

		usbf_ep_reset(&udc->ep[i]);
	}

	if (usbf_reg_readl(udc, USBF_REG_USB_STATUS) & USBF_USB_SPEED_MODE)
		udc->gadget.speed = USB_SPEED_HIGH;
	else
		udc->gadget.speed = USB_SPEED_FULL;

	/* Remote wakeup feature must be disabled on USB bus reset */
	udc->is_remote_wakeup = false;

	/* Enable endpoint zero */
	usbf_ep0_enable(&udc->ep[0]);

	if (udc->driver) {
		/* Signal the reset */
		spin_unlock(&udc->lock);
		usb_gadget_udc_reset(&udc->gadget, udc->driver);
		spin_lock(&udc->lock);
	}
}

static void usbf_driver_suspend(struct usbf_udc *udc)
{
	if (udc->is_usb_suspended) {
		dev_dbg(udc->dev, "already suspended\n");
		return;
	}

	dev_dbg(udc->dev, "do usb suspend\n");
	udc->is_usb_suspended = true;

	if (udc->driver && udc->driver->suspend) {
		spin_unlock(&udc->lock);
		udc->driver->suspend(&udc->gadget);
		spin_lock(&udc->lock);

		/* The datasheet tells to set the USB_CONTROL register SUSPEND
		 * bit when the USB bus suspend is detected.
		 * This bit stops the clocks (clocks for EPC, SIE, USBPHY) but
		 * these clocks seems not used only by the USB device. Some
		 * UARTs can be lost ...
		 * So, do not set the USB_CONTROL register SUSPEND bit.
		 */
	}
}

static void usbf_driver_resume(struct usbf_udc *udc)
{
	if (!udc->is_usb_suspended)
		return;

	dev_dbg(udc->dev, "do usb resume\n");
	udc->is_usb_suspended = false;

	if (udc->driver && udc->driver->resume) {
		spin_unlock(&udc->lock);
		udc->driver->resume(&udc->gadget);
		spin_lock(&udc->lock);
	}
}

static irqreturn_t usbf_epc_irq(int irq, void *_udc)
{
	struct usbf_udc *udc = (struct usbf_udc *)_udc;
	unsigned long flags;
	struct usbf_ep *ep;
	u32 int_sts;
	u32 int_en;
	int i;

	spin_lock_irqsave(&udc->lock, flags);

	int_en = usbf_reg_readl(udc, USBF_REG_USB_INT_ENA);
	int_sts = usbf_reg_readl(udc, USBF_REG_USB_INT_STA) & int_en;
	usbf_reg_writel(udc, USBF_REG_USB_INT_STA, ~int_sts);

	dev_dbg(udc->dev, "int_sts=0x%08x\n", int_sts);

	if (int_sts & USBF_USB_RSUM_INT) {
		dev_dbg(udc->dev, "handle resume\n");
		usbf_driver_resume(udc);
	}

	if (int_sts & USBF_USB_USB_RST_INT) {
		dev_dbg(udc->dev, "handle bus reset\n");
		usbf_driver_resume(udc);
		usbf_reset(udc);
	}

	if (int_sts & USBF_USB_SPEED_MODE_INT) {
		if (usbf_reg_readl(udc, USBF_REG_USB_STATUS) & USBF_USB_SPEED_MODE)
			udc->gadget.speed = USB_SPEED_HIGH;
		else
			udc->gadget.speed = USB_SPEED_FULL;
		dev_dbg(udc->dev, "handle speed change (%s)\n",
			udc->gadget.speed == USB_SPEED_HIGH ? "High" : "Full");
	}

	if (int_sts & USBF_USB_EPN_INT(0)) {
		usbf_driver_resume(udc);
		usbf_ep0_interrupt(&udc->ep[0]);
	}

	for (i = 1; i < ARRAY_SIZE(udc->ep); i++) {
		ep = &udc->ep[i];

		if (int_sts & USBF_USB_EPN_INT(i)) {
			usbf_driver_resume(udc);
			usbf_epn_interrupt(ep);
		}
	}

	if (int_sts & USBF_USB_SPND_INT) {
		dev_dbg(udc->dev, "handle suspend\n");
		usbf_driver_suspend(udc);
	}

	spin_unlock_irqrestore(&udc->lock, flags);

	return IRQ_HANDLED;
}

static irqreturn_t usbf_ahb_epc_irq(int irq, void *_udc)
{
	struct usbf_udc *udc = (struct usbf_udc *)_udc;
	unsigned long flags;
	struct usbf_ep *epn;
	u32 sysbint;
	void (*ep_action)(struct usbf_ep *epn);
	int i;

	spin_lock_irqsave(&udc->lock, flags);

	/* Read and ack interrupts */
	sysbint = usbf_reg_readl(udc, USBF_REG_AHBBINT);
	usbf_reg_writel(udc, USBF_REG_AHBBINT, sysbint);

	if ((sysbint & USBF_SYS_VBUS_INT) == USBF_SYS_VBUS_INT) {
		if (usbf_reg_readl(udc, USBF_REG_EPCTR) & USBF_SYS_VBUS_LEVEL) {
			dev_dbg(udc->dev, "handle vbus (1)\n");
			spin_unlock(&udc->lock);
			usb_udc_vbus_handler(&udc->gadget, true);
			usb_gadget_set_state(&udc->gadget, USB_STATE_POWERED);
			spin_lock(&udc->lock);
		} else {
			dev_dbg(udc->dev, "handle vbus (0)\n");
			udc->is_usb_suspended = false;
			spin_unlock(&udc->lock);
			usb_udc_vbus_handler(&udc->gadget, false);
			usb_gadget_set_state(&udc->gadget,
					     USB_STATE_NOTATTACHED);
			spin_lock(&udc->lock);
		}
	}

	for (i = 1; i < ARRAY_SIZE(udc->ep); i++) {
		if (sysbint & USBF_SYS_DMA_ENDINT_EPN(i)) {
			epn = &udc->ep[i];
			dev_dbg(epn->udc->dev,
				"ep%u handle DMA complete. action=%ps\n",
				epn->id, epn->bridge_on_dma_end);
			ep_action = epn->bridge_on_dma_end;
			if (ep_action) {
				epn->bridge_on_dma_end = NULL;
				ep_action(epn);
			}
		}
	}

	spin_unlock_irqrestore(&udc->lock, flags);

	return IRQ_HANDLED;
}

static int usbf_udc_start(struct usb_gadget *gadget,
			  struct usb_gadget_driver *driver)
{
	struct usbf_udc *udc = container_of(gadget, struct usbf_udc, gadget);
	unsigned long flags;

	dev_info(udc->dev, "start (driver '%s')\n", driver->driver.name);

	spin_lock_irqsave(&udc->lock, flags);

	/* hook up the driver */
	udc->driver = driver;

	/* Enable VBUS interrupt */
	usbf_reg_writel(udc, USBF_REG_AHBBINTEN, USBF_SYS_VBUS_INTEN);

	spin_unlock_irqrestore(&udc->lock, flags);

	return 0;
}

static int usbf_udc_stop(struct usb_gadget *gadget)
{
	struct usbf_udc *udc = container_of(gadget, struct usbf_udc, gadget);
	unsigned long flags;

	spin_lock_irqsave(&udc->lock, flags);

	/* Disable VBUS interrupt */
	usbf_reg_writel(udc, USBF_REG_AHBBINTEN, 0);

	udc->driver = NULL;

	spin_unlock_irqrestore(&udc->lock, flags);

	dev_info(udc->dev, "stopped\n");

	return 0;
}

static int usbf_get_frame(struct usb_gadget *gadget)
{
	struct usbf_udc *udc = container_of(gadget, struct usbf_udc, gadget);

	return USBF_USB_GET_FRAME(usbf_reg_readl(udc, USBF_REG_USB_ADDRESS));
}

static void usbf_attach(struct usbf_udc *udc)
{
	/* Enable USB signal to Function PHY
	 * D+ signal Pull-up
	 * Disable endpoint 0, it will be automatically enable when a USB reset
	 * is received.
	 * Disable the other endpoints
	 */
	usbf_reg_clrset(udc, USBF_REG_USB_CONTROL,
		USBF_USB_CONNECTB | USBF_USB_DEFAULT | USBF_USB_CONF,
		USBF_USB_PUE2);

	/* Enable reset and mode change interrupts */
	usbf_reg_bitset(udc, USBF_REG_USB_INT_ENA,
		USBF_USB_USB_RST_EN | USBF_USB_SPEED_MODE_EN | USBF_USB_RSUM_EN | USBF_USB_SPND_EN);
}

static void usbf_detach(struct usbf_udc *udc)
{
	int i;

	/* Disable interrupts */
	usbf_reg_writel(udc, USBF_REG_USB_INT_ENA, 0);

	for (i = 0; i < ARRAY_SIZE(udc->ep); i++) {
		if (udc->ep[i].disabled)
			continue;

		usbf_ep_reset(&udc->ep[i]);
	}

	/* Disable USB signal to Function PHY
	 * Do not Pull-up D+ signal
	 * Disable endpoint 0
	 * Disable the other endpoints
	 */
	usbf_reg_clrset(udc, USBF_REG_USB_CONTROL,
		USBF_USB_PUE2 | USBF_USB_DEFAULT | USBF_USB_CONF,
		USBF_USB_CONNECTB);
}

static int usbf_pullup(struct usb_gadget *gadget, int is_on)
{
	struct usbf_udc *udc = container_of(gadget, struct usbf_udc, gadget);
	unsigned long flags;

	dev_dbg(udc->dev, "pullup %d\n", is_on);

	spin_lock_irqsave(&udc->lock, flags);
	if (is_on)
		usbf_attach(udc);
	else
		usbf_detach(udc);
	spin_unlock_irqrestore(&udc->lock, flags);

	return 0;
}

static int usbf_udc_set_selfpowered(struct usb_gadget *gadget,
				    int is_selfpowered)
{
	struct usbf_udc *udc = container_of(gadget, struct usbf_udc, gadget);
	unsigned long flags;

	spin_lock_irqsave(&udc->lock, flags);
	gadget->is_selfpowered = (is_selfpowered != 0);
	spin_unlock_irqrestore(&udc->lock, flags);

	return 0;
}

static int usbf_udc_wakeup(struct usb_gadget *gadget)
{
	struct usbf_udc *udc = container_of(gadget, struct usbf_udc, gadget);
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&udc->lock, flags);

	if (!udc->is_remote_wakeup) {
		dev_dbg(udc->dev, "remote wakeup not allowed\n");
		ret = -EINVAL;
		goto end;
	}

	dev_dbg(udc->dev, "do wakeup\n");

	/* Send the resume signal */
	usbf_reg_bitset(udc, USBF_REG_USB_CONTROL, USBF_USB_RSUM_IN);
	usbf_reg_bitclr(udc, USBF_REG_USB_CONTROL, USBF_USB_RSUM_IN);

	ret = 0;
end:
	spin_unlock_irqrestore(&udc->lock, flags);
	return ret;
}

static struct usb_gadget_ops usbf_gadget_ops = {
	.get_frame = usbf_get_frame,
	.pullup = usbf_pullup,
	.udc_start = usbf_udc_start,
	.udc_stop = usbf_udc_stop,
	.set_selfpowered = usbf_udc_set_selfpowered,
	.wakeup = usbf_udc_wakeup,
};

static int usbf_epn_check(struct usbf_ep *epn)
{
	const char *type_txt;
	const char *buf_txt;
	int ret = 0;
	u32 ctrl;

	ctrl = usbf_ep_reg_readl(epn, USBF_REG_EPN_CONTROL);

	switch (ctrl & USBF_EPN_MODE_MASK) {
	case USBF_EPN_MODE_BULK:
		type_txt = "bulk";
		if (epn->ep.caps.type_control || epn->ep.caps.type_iso ||
		    !epn->ep.caps.type_bulk || epn->ep.caps.type_int) {
			dev_err(epn->udc->dev,
				"ep%u caps mismatch, bulk expected\n", epn->id);
			ret = -EINVAL;
		}
		break;
	case USBF_EPN_MODE_INTR:
		type_txt = "intr";
		if (epn->ep.caps.type_control || epn->ep.caps.type_iso ||
		    epn->ep.caps.type_bulk || !epn->ep.caps.type_int) {
			dev_err(epn->udc->dev,
				"ep%u caps mismatch, int expected\n", epn->id);
			ret = -EINVAL;
		}
		break;
	case USBF_EPN_MODE_ISO:
		type_txt = "iso";
		if (epn->ep.caps.type_control || !epn->ep.caps.type_iso ||
		    epn->ep.caps.type_bulk || epn->ep.caps.type_int) {
			dev_err(epn->udc->dev,
				"ep%u caps mismatch, iso expected\n", epn->id);
			ret = -EINVAL;
		}
		break;
	default:
		type_txt = "unknown";
		dev_err(epn->udc->dev, "ep%u unknown type\n", epn->id);
		ret = -EINVAL;
		break;
	}

	if (ctrl & USBF_EPN_BUF_TYPE_DOUBLE) {
		buf_txt = "double";
		if (!usbf_ep_info[epn->id].is_double) {
			dev_err(epn->udc->dev,
				"ep%u buffer mismatch, double expected\n",
				epn->id);
			ret = -EINVAL;
		}
	} else {
		buf_txt = "single";
		if (usbf_ep_info[epn->id].is_double) {
			dev_err(epn->udc->dev,
				"ep%u buffer mismatch, single expected\n",
				epn->id);
			ret = -EINVAL;
		}
	}

	dev_dbg(epn->udc->dev, "ep%u (%s) %s, %s buffer %u, checked %s\n",
		 epn->id, epn->ep.name, type_txt, buf_txt,
		 epn->ep.maxpacket_limit, ret ? "failed" : "ok");

	return ret;
}

static int usbf_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct usbf_udc *udc;
	struct usbf_ep *ep;
	unsigned int i;
	int irq;
	int ret;

	udc = devm_kzalloc(dev, sizeof(*udc), GFP_KERNEL);
	if (!udc)
		return -ENOMEM;
	platform_set_drvdata(pdev, udc);

	udc->dev = dev;
	spin_lock_init(&udc->lock);

	udc->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(udc->regs))
		return PTR_ERR(udc->regs);

	devm_pm_runtime_enable(&pdev->dev);
	ret = pm_runtime_resume_and_get(&pdev->dev);
	if (ret < 0)
		return ret;

	dev_info(dev, "USBF version: %08x\n",
		usbf_reg_readl(udc, USBF_REG_USBSSVER));

	/* Resetting the PLL is handled via the clock driver as it has common
	 * registers with USB Host
	 */
	usbf_reg_bitclr(udc, USBF_REG_EPCTR, USBF_SYS_EPC_RST);

	/* modify in register gadget process */
	udc->gadget.speed = USB_SPEED_FULL;
	udc->gadget.max_speed = USB_SPEED_HIGH;
	udc->gadget.ops = &usbf_gadget_ops;

	udc->gadget.name = dev->driver->name;
	udc->gadget.dev.parent = dev;
	udc->gadget.ep0 = &udc->ep[0].ep;

	/* The hardware DMA controller needs dma addresses aligned on 32bit.
	 * A fallback to pio is done if DMA addresses are not aligned.
	 */
	udc->gadget.quirk_avoids_skb_reserve = 1;

	INIT_LIST_HEAD(&udc->gadget.ep_list);
	/* we have a canned request structure to allow sending packets as reply
	 * to get_status requests
	 */
	INIT_LIST_HEAD(&udc->setup_reply.queue);

	for (i = 0; i < ARRAY_SIZE(udc->ep); i++) {
		ep = &udc->ep[i];

		if (!(usbf_reg_readl(udc, USBF_REG_USBSSCONF) &
		      USBF_SYS_EP_AVAILABLE(i))) {
			continue;
		}

		INIT_LIST_HEAD(&ep->queue);

		ep->id = i;
		ep->disabled = 1;
		ep->udc = udc;
		ep->ep.ops = &usbf_ep_ops;
		ep->ep.name = usbf_ep_info[i].name;
		ep->ep.caps = usbf_ep_info[i].caps;
		usb_ep_set_maxpacket_limit(&ep->ep,
					   usbf_ep_info[i].maxpacket_limit);

		if (ep->id == 0) {
			ep->regs = ep->udc->regs + USBF_BASE_EP0;
		} else {
			ep->regs = ep->udc->regs + USBF_BASE_EPN(ep->id - 1);
			ret = usbf_epn_check(ep);
			if (ret)
				return ret;
			if (usbf_reg_readl(udc, USBF_REG_USBSSCONF) &
			    USBF_SYS_DMA_AVAILABLE(i)) {
				ep->dma_regs = ep->udc->regs +
					       USBF_BASE_DMA_EPN(ep->id - 1);
			}
			list_add_tail(&ep->ep.ep_list, &udc->gadget.ep_list);
		}
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;
	ret = devm_request_irq(dev, irq, usbf_epc_irq, 0, "usbf-epc", udc);
	if (ret) {
		dev_err(dev, "cannot request irq %d err %d\n", irq, ret);
		return ret;
	}

	irq = platform_get_irq(pdev, 1);
	if (irq < 0)
		return irq;
	ret = devm_request_irq(dev, irq, usbf_ahb_epc_irq, 0, "usbf-ahb-epc", udc);
	if (ret) {
		dev_err(dev, "cannot request irq %d err %d\n", irq, ret);
		return ret;
	}

	usbf_reg_bitset(udc, USBF_REG_AHBMCTR, USBF_SYS_WBURST_TYPE);

	usbf_reg_bitset(udc, USBF_REG_USB_CONTROL,
		USBF_USB_INT_SEL | USBF_USB_SOF_RCV | USBF_USB_SOF_CLK_MODE);

	ret = usb_add_gadget_udc(dev, &udc->gadget);
	if (ret)
		return ret;

	return 0;
}

static void usbf_remove(struct platform_device *pdev)
{
	struct usbf_udc *udc = platform_get_drvdata(pdev);

	usb_del_gadget_udc(&udc->gadget);

	pm_runtime_put(&pdev->dev);
}

static const struct of_device_id usbf_match[] = {
	{ .compatible = "renesas,rzn1-usbf" },
	{} /* sentinel */
};
MODULE_DEVICE_TABLE(of, usbf_match);

static struct platform_driver udc_driver = {
	.driver = {
		.name = "usbf_renesas",
		.of_match_table = usbf_match,
	},
	.probe          = usbf_probe,
	.remove_new     = usbf_remove,
};

module_platform_driver(udc_driver);

MODULE_AUTHOR("Herve Codina <herve.codina@bootlin.com>");
MODULE_DESCRIPTION("Renesas R-Car Gen3 & RZ/N1 USB Function driver");
MODULE_LICENSE("GPL");
