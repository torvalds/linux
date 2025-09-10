// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas USB3.0 Peripheral driver (USB gadget)
 *
 * Copyright (C) 2015-2017  Renesas Electronics Corporation
 */

#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/extcon-provider.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <linux/usb/of.h>
#include <linux/usb/role.h>
#include <linux/usb/rzv2m_usb3drd.h>

/* register definitions */
#define USB3_AXI_INT_STA	0x008
#define USB3_AXI_INT_ENA	0x00c
#define USB3_DMA_INT_STA	0x010
#define USB3_DMA_INT_ENA	0x014
#define USB3_DMA_CH0_CON(n)	(0x030 + ((n) - 1) * 0x10) /* n = 1 to 4 */
#define USB3_DMA_CH0_PRD_ADR(n)	(0x034 + ((n) - 1) * 0x10) /* n = 1 to 4 */
#define USB3_USB_COM_CON	0x200
#define USB3_USB20_CON		0x204
#define USB3_USB30_CON		0x208
#define USB3_USB_STA		0x210
#define USB3_DRD_CON(p)		((p)->is_rzv2m ? 0x400 : 0x218)
#define USB3_USB_INT_STA_1	0x220
#define USB3_USB_INT_STA_2	0x224
#define USB3_USB_INT_ENA_1	0x228
#define USB3_USB_INT_ENA_2	0x22c
#define USB3_STUP_DAT_0		0x230
#define USB3_STUP_DAT_1		0x234
#define USB3_USB_OTG_STA(p)	((p)->is_rzv2m ? 0x410 : 0x268)
#define USB3_USB_OTG_INT_STA(p)	((p)->is_rzv2m ? 0x414 : 0x26c)
#define USB3_USB_OTG_INT_ENA(p)	((p)->is_rzv2m ? 0x418 : 0x270)
#define USB3_P0_MOD		0x280
#define USB3_P0_CON		0x288
#define USB3_P0_STA		0x28c
#define USB3_P0_INT_STA		0x290
#define USB3_P0_INT_ENA		0x294
#define USB3_P0_LNG		0x2a0
#define USB3_P0_READ		0x2a4
#define USB3_P0_WRITE		0x2a8
#define USB3_PIPE_COM		0x2b0
#define USB3_PN_MOD		0x2c0
#define USB3_PN_RAMMAP		0x2c4
#define USB3_PN_CON		0x2c8
#define USB3_PN_STA		0x2cc
#define USB3_PN_INT_STA		0x2d0
#define USB3_PN_INT_ENA		0x2d4
#define USB3_PN_LNG		0x2e0
#define USB3_PN_READ		0x2e4
#define USB3_PN_WRITE		0x2e8
#define USB3_SSIFCMD		0x340

/* AXI_INT_ENA and AXI_INT_STA */
#define AXI_INT_DMAINT		BIT(31)
#define AXI_INT_EPCINT		BIT(30)
/* PRD's n = from 1 to 4 */
#define AXI_INT_PRDEN_CLR_STA_SHIFT(n)	(16 + (n) - 1)
#define AXI_INT_PRDERR_STA_SHIFT(n)	(0 + (n) - 1)
#define AXI_INT_PRDEN_CLR_STA(n)	(1 << AXI_INT_PRDEN_CLR_STA_SHIFT(n))
#define AXI_INT_PRDERR_STA(n)		(1 << AXI_INT_PRDERR_STA_SHIFT(n))

/* DMA_INT_ENA and DMA_INT_STA */
#define DMA_INT(n)		BIT(n)

/* DMA_CH0_CONn */
#define DMA_CON_PIPE_DIR	BIT(15)		/* 1: In Transfer */
#define DMA_CON_PIPE_NO_SHIFT	8
#define DMA_CON_PIPE_NO_MASK	GENMASK(12, DMA_CON_PIPE_NO_SHIFT)
#define DMA_COM_PIPE_NO(n)	(((n) << DMA_CON_PIPE_NO_SHIFT) & \
					 DMA_CON_PIPE_NO_MASK)
#define DMA_CON_PRD_EN		BIT(0)

/* LCLKSEL */
#define LCLKSEL_LSEL		BIT(18)

/* USB_COM_CON */
#define USB_COM_CON_CONF		BIT(24)
#define USB_COM_CON_PN_WDATAIF_NL	BIT(23)
#define USB_COM_CON_PN_RDATAIF_NL	BIT(22)
#define USB_COM_CON_PN_LSTTR_PP		BIT(21)
#define USB_COM_CON_SPD_MODE		BIT(17)
#define USB_COM_CON_EP0_EN		BIT(16)
#define USB_COM_CON_DEV_ADDR_SHIFT	8
#define USB_COM_CON_DEV_ADDR_MASK	GENMASK(14, USB_COM_CON_DEV_ADDR_SHIFT)
#define USB_COM_CON_DEV_ADDR(n)		(((n) << USB_COM_CON_DEV_ADDR_SHIFT) & \
					 USB_COM_CON_DEV_ADDR_MASK)
#define USB_COM_CON_RX_DETECTION	BIT(1)
#define USB_COM_CON_PIPE_CLR		BIT(0)

/* USB20_CON */
#define USB20_CON_B2_PUE		BIT(31)
#define USB20_CON_B2_SUSPEND		BIT(24)
#define USB20_CON_B2_CONNECT		BIT(17)
#define USB20_CON_B2_TSTMOD_SHIFT	8
#define USB20_CON_B2_TSTMOD_MASK	GENMASK(10, USB20_CON_B2_TSTMOD_SHIFT)
#define USB20_CON_B2_TSTMOD(n)		(((n) << USB20_CON_B2_TSTMOD_SHIFT) & \
					 USB20_CON_B2_TSTMOD_MASK)
#define USB20_CON_B2_TSTMOD_EN		BIT(0)

/* USB30_CON */
#define USB30_CON_POW_SEL_SHIFT		24
#define USB30_CON_POW_SEL_MASK		GENMASK(26, USB30_CON_POW_SEL_SHIFT)
#define USB30_CON_POW_SEL_IN_U3		BIT(26)
#define USB30_CON_POW_SEL_IN_DISCON	0
#define USB30_CON_POW_SEL_P2_TO_P0	BIT(25)
#define USB30_CON_POW_SEL_P0_TO_P3	BIT(24)
#define USB30_CON_POW_SEL_P0_TO_P2	0
#define USB30_CON_B3_PLLWAKE		BIT(23)
#define USB30_CON_B3_CONNECT		BIT(17)
#define USB30_CON_B3_HOTRST_CMP		BIT(1)

/* USB_STA */
#define USB_STA_SPEED_MASK	(BIT(2) | BIT(1))
#define USB_STA_SPEED_HS	BIT(2)
#define USB_STA_SPEED_FS	BIT(1)
#define USB_STA_SPEED_SS	0
#define USB_STA_VBUS_STA	BIT(0)

/* DRD_CON */
#define DRD_CON_PERI_RST	BIT(31)		/* rzv2m only */
#define DRD_CON_HOST_RST	BIT(30)		/* rzv2m only */
#define DRD_CON_PERI_CON	BIT(24)
#define DRD_CON_VBOUT		BIT(0)

/* USB_INT_ENA_1 and USB_INT_STA_1 */
#define USB_INT_1_B3_PLLWKUP	BIT(31)
#define USB_INT_1_B3_LUPSUCS	BIT(30)
#define USB_INT_1_B3_DISABLE	BIT(27)
#define USB_INT_1_B3_WRMRST	BIT(21)
#define USB_INT_1_B3_HOTRST	BIT(20)
#define USB_INT_1_B2_USBRST	BIT(12)
#define USB_INT_1_B2_L1SPND	BIT(11)
#define USB_INT_1_B2_SPND	BIT(9)
#define USB_INT_1_B2_RSUM	BIT(8)
#define USB_INT_1_SPEED		BIT(1)
#define USB_INT_1_VBUS_CNG	BIT(0)

/* USB_INT_ENA_2 and USB_INT_STA_2 */
#define USB_INT_2_PIPE(n)	BIT(n)

/* USB_OTG_STA, USB_OTG_INT_STA and USB_OTG_INT_ENA */
#define USB_OTG_IDMON(p)	((p)->is_rzv2m ? BIT(0) : BIT(4))

/* P0_MOD */
#define P0_MOD_DIR		BIT(6)

/* P0_CON and PN_CON */
#define PX_CON_BYTE_EN_MASK		(BIT(10) | BIT(9))
#define PX_CON_BYTE_EN_SHIFT		9
#define PX_CON_BYTE_EN_BYTES(n)		(((n) << PX_CON_BYTE_EN_SHIFT) & \
					 PX_CON_BYTE_EN_MASK)
#define PX_CON_SEND			BIT(8)

/* P0_CON */
#define P0_CON_ST_RES_MASK		(BIT(27) | BIT(26))
#define P0_CON_ST_RES_FORCE_STALL	BIT(27)
#define P0_CON_ST_RES_NORMAL		BIT(26)
#define P0_CON_ST_RES_FORCE_NRDY	0
#define P0_CON_OT_RES_MASK		(BIT(25) | BIT(24))
#define P0_CON_OT_RES_FORCE_STALL	BIT(25)
#define P0_CON_OT_RES_NORMAL		BIT(24)
#define P0_CON_OT_RES_FORCE_NRDY	0
#define P0_CON_IN_RES_MASK		(BIT(17) | BIT(16))
#define P0_CON_IN_RES_FORCE_STALL	BIT(17)
#define P0_CON_IN_RES_NORMAL		BIT(16)
#define P0_CON_IN_RES_FORCE_NRDY	0
#define P0_CON_RES_WEN			BIT(7)
#define P0_CON_BCLR			BIT(1)

/* P0_STA and PN_STA */
#define PX_STA_BUFSTS		BIT(0)

/* P0_INT_ENA and P0_INT_STA */
#define P0_INT_STSED		BIT(18)
#define P0_INT_STSST		BIT(17)
#define P0_INT_SETUP		BIT(16)
#define P0_INT_RCVNL		BIT(8)
#define P0_INT_ERDY		BIT(7)
#define P0_INT_FLOW		BIT(6)
#define P0_INT_STALL		BIT(2)
#define P0_INT_NRDY		BIT(1)
#define P0_INT_BFRDY		BIT(0)
#define P0_INT_ALL_BITS		(P0_INT_STSED | P0_INT_SETUP | P0_INT_BFRDY)

/* PN_MOD */
#define PN_MOD_DIR		BIT(6)
#define PN_MOD_TYPE_SHIFT	4
#define PN_MOD_TYPE_MASK	GENMASK(5, PN_MOD_TYPE_SHIFT)
#define PN_MOD_TYPE(n)		(((n) << PN_MOD_TYPE_SHIFT) & \
				 PN_MOD_TYPE_MASK)
#define PN_MOD_EPNUM_MASK	GENMASK(3, 0)
#define PN_MOD_EPNUM(n)		((n) & PN_MOD_EPNUM_MASK)

/* PN_RAMMAP */
#define PN_RAMMAP_RAMAREA_SHIFT	29
#define PN_RAMMAP_RAMAREA_MASK	GENMASK(31, PN_RAMMAP_RAMAREA_SHIFT)
#define PN_RAMMAP_RAMAREA_16KB	BIT(31)
#define PN_RAMMAP_RAMAREA_8KB	(BIT(30) | BIT(29))
#define PN_RAMMAP_RAMAREA_4KB	BIT(30)
#define PN_RAMMAP_RAMAREA_2KB	BIT(29)
#define PN_RAMMAP_RAMAREA_1KB	0
#define PN_RAMMAP_MPKT_SHIFT	16
#define PN_RAMMAP_MPKT_MASK	GENMASK(26, PN_RAMMAP_MPKT_SHIFT)
#define PN_RAMMAP_MPKT(n)	(((n) << PN_RAMMAP_MPKT_SHIFT) & \
				 PN_RAMMAP_MPKT_MASK)
#define PN_RAMMAP_RAMIF_SHIFT	14
#define PN_RAMMAP_RAMIF_MASK	GENMASK(15, PN_RAMMAP_RAMIF_SHIFT)
#define PN_RAMMAP_RAMIF(n)	(((n) << PN_RAMMAP_RAMIF_SHIFT) & \
				 PN_RAMMAP_RAMIF_MASK)
#define PN_RAMMAP_BASEAD_MASK	GENMASK(13, 0)
#define PN_RAMMAP_BASEAD(offs)	(((offs) >> 3) & PN_RAMMAP_BASEAD_MASK)
#define PN_RAMMAP_DATA(area, ramif, basead)	((PN_RAMMAP_##area) | \
						 (PN_RAMMAP_RAMIF(ramif)) | \
						 (PN_RAMMAP_BASEAD(basead)))

/* PN_CON */
#define PN_CON_EN		BIT(31)
#define PN_CON_DATAIF_EN	BIT(30)
#define PN_CON_RES_MASK		(BIT(17) | BIT(16))
#define PN_CON_RES_FORCE_STALL	BIT(17)
#define PN_CON_RES_NORMAL	BIT(16)
#define PN_CON_RES_FORCE_NRDY	0
#define PN_CON_LAST		BIT(11)
#define PN_CON_RES_WEN		BIT(7)
#define PN_CON_CLR		BIT(0)

/* PN_INT_STA and PN_INT_ENA */
#define PN_INT_LSTTR	BIT(4)
#define PN_INT_BFRDY	BIT(0)

/* USB3_SSIFCMD */
#define SSIFCMD_URES_U2		BIT(9)
#define SSIFCMD_URES_U1		BIT(8)
#define SSIFCMD_UDIR_U2		BIT(7)
#define SSIFCMD_UDIR_U1		BIT(6)
#define SSIFCMD_UREQ_U2		BIT(5)
#define SSIFCMD_UREQ_U1		BIT(4)

#define USB3_EP0_SS_MAX_PACKET_SIZE	512
#define USB3_EP0_HSFS_MAX_PACKET_SIZE	64
#define USB3_EP0_BUF_SIZE		8
#define USB3_MAX_NUM_PIPES(p)		((p)->is_rzv2m ? 16 : 6)	/* This includes PIPE 0 */
#define USB3_WAIT_US			3
#define USB3_DMA_NUM_SETTING_AREA	4
/*
 * To avoid double-meaning of "0" (xferred 65536 bytes or received zlp if
 * buffer size is 65536), this driver uses the maximum size per a entry is
 * 32768 bytes.
 */
#define USB3_DMA_MAX_XFER_SIZE		32768
#define USB3_DMA_PRD_SIZE		4096

struct renesas_usb3;

/* Physical Region Descriptor Table */
struct renesas_usb3_prd {
	u32 word1;
#define USB3_PRD1_E		BIT(30)		/* the end of chain */
#define USB3_PRD1_U		BIT(29)		/* completion of transfer */
#define USB3_PRD1_D		BIT(28)		/* Error occurred */
#define USB3_PRD1_INT		BIT(27)		/* Interrupt occurred */
#define USB3_PRD1_LST		BIT(26)		/* Last Packet */
#define USB3_PRD1_B_INC		BIT(24)
#define USB3_PRD1_MPS_8		0
#define USB3_PRD1_MPS_16	BIT(21)
#define USB3_PRD1_MPS_32	BIT(22)
#define USB3_PRD1_MPS_64	(BIT(22) | BIT(21))
#define USB3_PRD1_MPS_512	BIT(23)
#define USB3_PRD1_MPS_1024	(BIT(23) | BIT(21))
#define USB3_PRD1_MPS_RESERVED	(BIT(23) | BIT(22) | BIT(21))
#define USB3_PRD1_SIZE_MASK	GENMASK(15, 0)

	u32 bap;
};
#define USB3_DMA_NUM_PRD_ENTRIES	(USB3_DMA_PRD_SIZE / \
					  sizeof(struct renesas_usb3_prd))
#define USB3_DMA_MAX_XFER_SIZE_ALL_PRDS	(USB3_DMA_PRD_SIZE / \
					 sizeof(struct renesas_usb3_prd) * \
					 USB3_DMA_MAX_XFER_SIZE)

struct renesas_usb3_dma {
	struct renesas_usb3_prd *prd;
	dma_addr_t prd_dma;
	int num;	/* Setting area number (from 1 to 4) */
	bool used;
};

struct renesas_usb3_request {
	struct usb_request	req;
	struct list_head	queue;
};

#define USB3_EP_NAME_SIZE	16
struct renesas_usb3_ep {
	struct usb_ep ep;
	struct renesas_usb3 *usb3;
	struct renesas_usb3_dma *dma;
	int num;
	char ep_name[USB3_EP_NAME_SIZE];
	struct list_head queue;
	u32 rammap_val;
	bool dir_in;
	bool halt;
	bool wedge;
	bool started;
};

struct renesas_usb3_priv {
	int ramsize_per_ramif;		/* unit = bytes */
	int num_ramif;
	int ramsize_per_pipe;		/* unit = bytes */
	bool workaround_for_vbus;	/* if true, don't check vbus signal */
	bool is_rzv2m;			/* if true, RZ/V2M SoC */
};

struct renesas_usb3 {
	void __iomem *reg;
	void __iomem *drd_reg;
	struct reset_control *usbp_rstc;

	struct usb_gadget gadget;
	struct usb_gadget_driver *driver;
	struct extcon_dev *extcon;
	struct work_struct extcon_work;
	struct phy *phy;
	struct dentry *dentry;

	struct usb_role_switch *role_sw;
	struct device *host_dev;
	struct work_struct role_work;
	enum usb_role role;

	struct renesas_usb3_ep *usb3_ep;
	int num_usb3_eps;

	struct renesas_usb3_dma dma[USB3_DMA_NUM_SETTING_AREA];

	spinlock_t lock;
	int disabled_count;

	struct usb_request *ep0_req;

	enum usb_role connection_state;
	u16 test_mode;
	u8 ep0_buf[USB3_EP0_BUF_SIZE];
	bool softconnect;
	bool workaround_for_vbus;
	bool extcon_host;		/* check id and set EXTCON_USB_HOST */
	bool extcon_usb;		/* check vbus and set EXTCON_USB */
	bool forced_b_device;
	bool start_to_connect;
	bool role_sw_by_connector;
	bool is_rzv2m;
};

#define gadget_to_renesas_usb3(_gadget)	\
		container_of(_gadget, struct renesas_usb3, gadget)
#define renesas_usb3_to_gadget(renesas_usb3) (&renesas_usb3->gadget)
#define usb3_to_dev(_usb3)	(_usb3->gadget.dev.parent)

#define usb_ep_to_usb3_ep(_ep) container_of(_ep, struct renesas_usb3_ep, ep)
#define usb3_ep_to_usb3(_usb3_ep) (_usb3_ep->usb3)
#define usb_req_to_usb3_req(_req) container_of(_req, \
					    struct renesas_usb3_request, req)

#define usb3_get_ep(usb3, n) ((usb3)->usb3_ep + (n))
#define usb3_for_each_ep(usb3_ep, usb3, i)			\
		for ((i) = 0, usb3_ep = usb3_get_ep(usb3, (i));	\
		     (i) < (usb3)->num_usb3_eps;		\
		     (i)++, usb3_ep = usb3_get_ep(usb3, (i)))

#define usb3_get_dma(usb3, i)	(&(usb3)->dma[i])
#define usb3_for_each_dma(usb3, dma, i)				\
		for ((i) = 0, dma = usb3_get_dma((usb3), (i));	\
		     (i) < USB3_DMA_NUM_SETTING_AREA;		\
		     (i)++, dma = usb3_get_dma((usb3), (i)))

static const char udc_name[] = "renesas_usb3";

static bool use_dma = 1;
module_param(use_dma, bool, 0644);
MODULE_PARM_DESC(use_dma, "use dedicated DMAC");

static void usb3_write(struct renesas_usb3 *usb3, u32 data, u32 offs)
{
	iowrite32(data, usb3->reg + offs);
}

static u32 usb3_read(struct renesas_usb3 *usb3, u32 offs)
{
	return ioread32(usb3->reg + offs);
}

static void usb3_set_bit(struct renesas_usb3 *usb3, u32 bits, u32 offs)
{
	u32 val = usb3_read(usb3, offs);

	val |= bits;
	usb3_write(usb3, val, offs);
}

static void usb3_clear_bit(struct renesas_usb3 *usb3, u32 bits, u32 offs)
{
	u32 val = usb3_read(usb3, offs);

	val &= ~bits;
	usb3_write(usb3, val, offs);
}

static void usb3_drd_write(struct renesas_usb3 *usb3, u32 data, u32 offs)
{
	void __iomem *reg;

	if (usb3->is_rzv2m)
		reg = usb3->drd_reg + offs - USB3_DRD_CON(usb3);
	else
		reg = usb3->reg + offs;

	iowrite32(data, reg);
}

static u32 usb3_drd_read(struct renesas_usb3 *usb3, u32 offs)
{
	void __iomem *reg;

	if (usb3->is_rzv2m)
		reg = usb3->drd_reg + offs - USB3_DRD_CON(usb3);
	else
		reg = usb3->reg + offs;

	return ioread32(reg);
}

static void usb3_drd_set_bit(struct renesas_usb3 *usb3, u32 bits, u32 offs)
{
	u32 val = usb3_drd_read(usb3, offs);

	val |= bits;
	usb3_drd_write(usb3, val, offs);
}

static void usb3_drd_clear_bit(struct renesas_usb3 *usb3, u32 bits, u32 offs)
{
	u32 val = usb3_drd_read(usb3, offs);

	val &= ~bits;
	usb3_drd_write(usb3, val, offs);
}

static int usb3_wait(struct renesas_usb3 *usb3, u32 reg, u32 mask,
		     u32 expected)
{
	int i;

	for (i = 0; i < USB3_WAIT_US; i++) {
		if ((usb3_read(usb3, reg) & mask) == expected)
			return 0;
		udelay(1);
	}

	dev_dbg(usb3_to_dev(usb3), "%s: timed out (%8x, %08x, %08x)\n",
		__func__, reg, mask, expected);

	return -EBUSY;
}

static void renesas_usb3_extcon_work(struct work_struct *work)
{
	struct renesas_usb3 *usb3 = container_of(work, struct renesas_usb3,
						 extcon_work);

	extcon_set_state_sync(usb3->extcon, EXTCON_USB_HOST, usb3->extcon_host);
	extcon_set_state_sync(usb3->extcon, EXTCON_USB, usb3->extcon_usb);
}

static void usb3_enable_irq_1(struct renesas_usb3 *usb3, u32 bits)
{
	usb3_set_bit(usb3, bits, USB3_USB_INT_ENA_1);
}

static void usb3_disable_irq_1(struct renesas_usb3 *usb3, u32 bits)
{
	usb3_clear_bit(usb3, bits, USB3_USB_INT_ENA_1);
}

static void usb3_enable_pipe_irq(struct renesas_usb3 *usb3, int num)
{
	usb3_set_bit(usb3, USB_INT_2_PIPE(num), USB3_USB_INT_ENA_2);
}

static void usb3_disable_pipe_irq(struct renesas_usb3 *usb3, int num)
{
	usb3_clear_bit(usb3, USB_INT_2_PIPE(num), USB3_USB_INT_ENA_2);
}

static bool usb3_is_host(struct renesas_usb3 *usb3)
{
	return !(usb3_drd_read(usb3, USB3_DRD_CON(usb3)) & DRD_CON_PERI_CON);
}

static void usb3_init_axi_bridge(struct renesas_usb3 *usb3)
{
	/* Set AXI_INT */
	usb3_write(usb3, ~0, USB3_DMA_INT_STA);
	usb3_write(usb3, 0, USB3_DMA_INT_ENA);
	usb3_set_bit(usb3, AXI_INT_DMAINT | AXI_INT_EPCINT, USB3_AXI_INT_ENA);
}

static void usb3_init_epc_registers(struct renesas_usb3 *usb3)
{
	usb3_write(usb3, ~0, USB3_USB_INT_STA_1);
	if (!usb3->workaround_for_vbus)
		usb3_enable_irq_1(usb3, USB_INT_1_VBUS_CNG);
}

static bool usb3_wakeup_usb2_phy(struct renesas_usb3 *usb3)
{
	if (!(usb3_read(usb3, USB3_USB20_CON) & USB20_CON_B2_SUSPEND))
		return true;	/* already waked it up */

	usb3_clear_bit(usb3, USB20_CON_B2_SUSPEND, USB3_USB20_CON);
	usb3_enable_irq_1(usb3, USB_INT_1_B2_RSUM);

	return false;
}

static void usb3_usb2_pullup(struct renesas_usb3 *usb3, int pullup)
{
	u32 bits = USB20_CON_B2_PUE | USB20_CON_B2_CONNECT;

	if (usb3->softconnect && pullup)
		usb3_set_bit(usb3, bits, USB3_USB20_CON);
	else
		usb3_clear_bit(usb3, bits, USB3_USB20_CON);
}

static void usb3_set_test_mode(struct renesas_usb3 *usb3)
{
	u32 val = usb3_read(usb3, USB3_USB20_CON);

	val &= ~USB20_CON_B2_TSTMOD_MASK;
	val |= USB20_CON_B2_TSTMOD(usb3->test_mode);
	usb3_write(usb3, val | USB20_CON_B2_TSTMOD_EN, USB3_USB20_CON);
	if (!usb3->test_mode)
		usb3_clear_bit(usb3, USB20_CON_B2_TSTMOD_EN, USB3_USB20_CON);
}

static void usb3_start_usb2_connection(struct renesas_usb3 *usb3)
{
	usb3->disabled_count++;
	usb3_set_bit(usb3, USB_COM_CON_EP0_EN, USB3_USB_COM_CON);
	usb3_set_bit(usb3, USB_COM_CON_SPD_MODE, USB3_USB_COM_CON);
	usb3_usb2_pullup(usb3, 1);
}

static int usb3_is_usb3_phy_in_u3(struct renesas_usb3 *usb3)
{
	return usb3_read(usb3, USB3_USB30_CON) & USB30_CON_POW_SEL_IN_U3;
}

static bool usb3_wakeup_usb3_phy(struct renesas_usb3 *usb3)
{
	if (!usb3_is_usb3_phy_in_u3(usb3))
		return true;	/* already waked it up */

	usb3_set_bit(usb3, USB30_CON_B3_PLLWAKE, USB3_USB30_CON);
	usb3_enable_irq_1(usb3, USB_INT_1_B3_PLLWKUP);

	return false;
}

static u16 usb3_feature_get_un_enabled(struct renesas_usb3 *usb3)
{
	u32 mask_u2 = SSIFCMD_UDIR_U2 | SSIFCMD_UREQ_U2;
	u32 mask_u1 = SSIFCMD_UDIR_U1 | SSIFCMD_UREQ_U1;
	u32 val = usb3_read(usb3, USB3_SSIFCMD);
	u16 ret = 0;

	/* Enables {U2,U1} if the bits of UDIR and UREQ are set to 0 */
	if (!(val & mask_u2))
		ret |= 1 << USB_DEV_STAT_U2_ENABLED;
	if (!(val & mask_u1))
		ret |= 1 << USB_DEV_STAT_U1_ENABLED;

	return ret;
}

static void usb3_feature_u2_enable(struct renesas_usb3 *usb3, bool enable)
{
	u32 bits = SSIFCMD_UDIR_U2 | SSIFCMD_UREQ_U2;

	/* Enables U2 if the bits of UDIR and UREQ are set to 0 */
	if (enable)
		usb3_clear_bit(usb3, bits, USB3_SSIFCMD);
	else
		usb3_set_bit(usb3, bits, USB3_SSIFCMD);
}

static void usb3_feature_u1_enable(struct renesas_usb3 *usb3, bool enable)
{
	u32 bits = SSIFCMD_UDIR_U1 | SSIFCMD_UREQ_U1;

	/* Enables U1 if the bits of UDIR and UREQ are set to 0 */
	if (enable)
		usb3_clear_bit(usb3, bits, USB3_SSIFCMD);
	else
		usb3_set_bit(usb3, bits, USB3_SSIFCMD);
}

static void usb3_start_operation_for_usb3(struct renesas_usb3 *usb3)
{
	usb3_set_bit(usb3, USB_COM_CON_EP0_EN, USB3_USB_COM_CON);
	usb3_clear_bit(usb3, USB_COM_CON_SPD_MODE, USB3_USB_COM_CON);
	usb3_set_bit(usb3, USB30_CON_B3_CONNECT, USB3_USB30_CON);
}

static void usb3_start_usb3_connection(struct renesas_usb3 *usb3)
{
	usb3_start_operation_for_usb3(usb3);
	usb3_set_bit(usb3, USB_COM_CON_RX_DETECTION, USB3_USB_COM_CON);

	usb3_enable_irq_1(usb3, USB_INT_1_B3_LUPSUCS | USB_INT_1_B3_DISABLE |
			  USB_INT_1_SPEED);
}

static void usb3_stop_usb3_connection(struct renesas_usb3 *usb3)
{
	usb3_clear_bit(usb3, USB30_CON_B3_CONNECT, USB3_USB30_CON);
}

static void usb3_transition_to_default_state(struct renesas_usb3 *usb3,
					     bool is_usb3)
{
	usb3_set_bit(usb3, USB_INT_2_PIPE(0), USB3_USB_INT_ENA_2);
	usb3_write(usb3, P0_INT_ALL_BITS, USB3_P0_INT_STA);
	usb3_set_bit(usb3, P0_INT_ALL_BITS, USB3_P0_INT_ENA);

	if (is_usb3)
		usb3_enable_irq_1(usb3, USB_INT_1_B3_WRMRST |
				  USB_INT_1_B3_HOTRST);
	else
		usb3_enable_irq_1(usb3, USB_INT_1_B2_SPND |
				  USB_INT_1_B2_L1SPND | USB_INT_1_B2_USBRST);
}

static void usb3_connect(struct renesas_usb3 *usb3)
{
	if (usb3_wakeup_usb3_phy(usb3))
		usb3_start_usb3_connection(usb3);
}

static void usb3_reset_epc(struct renesas_usb3 *usb3)
{
	usb3_clear_bit(usb3, USB_COM_CON_CONF, USB3_USB_COM_CON);
	usb3_clear_bit(usb3, USB_COM_CON_EP0_EN, USB3_USB_COM_CON);
	usb3_set_bit(usb3, USB_COM_CON_PIPE_CLR, USB3_USB_COM_CON);
	usb3->test_mode = 0;
	usb3_set_test_mode(usb3);
}

static void usb3_disconnect(struct renesas_usb3 *usb3)
{
	usb3->disabled_count = 0;
	usb3_usb2_pullup(usb3, 0);
	usb3_clear_bit(usb3, USB30_CON_B3_CONNECT, USB3_USB30_CON);
	usb3_reset_epc(usb3);
	usb3_disable_irq_1(usb3, USB_INT_1_B2_RSUM | USB_INT_1_B3_PLLWKUP |
			   USB_INT_1_B3_LUPSUCS | USB_INT_1_B3_DISABLE |
			   USB_INT_1_SPEED | USB_INT_1_B3_WRMRST |
			   USB_INT_1_B3_HOTRST | USB_INT_1_B2_SPND |
			   USB_INT_1_B2_L1SPND | USB_INT_1_B2_USBRST);
	usb3_clear_bit(usb3, USB_COM_CON_SPD_MODE, USB3_USB_COM_CON);
	usb3_init_epc_registers(usb3);

	if (usb3->driver)
		usb3->driver->disconnect(&usb3->gadget);
}

static void usb3_check_vbus(struct renesas_usb3 *usb3)
{
	if (usb3->workaround_for_vbus) {
		usb3_connect(usb3);
	} else {
		usb3->extcon_usb = !!(usb3_read(usb3, USB3_USB_STA) &
							USB_STA_VBUS_STA);
		if (usb3->extcon_usb)
			usb3_connect(usb3);
		else
			usb3_disconnect(usb3);

		schedule_work(&usb3->extcon_work);
	}
}

static void renesas_usb3_role_work(struct work_struct *work)
{
	struct renesas_usb3 *usb3 =
			container_of(work, struct renesas_usb3, role_work);

	usb_role_switch_set_role(usb3->role_sw, usb3->role);
}

static void usb3_set_mode(struct renesas_usb3 *usb3, bool host)
{
	if (usb3->is_rzv2m) {
		if (host) {
			usb3_drd_set_bit(usb3, DRD_CON_PERI_RST, USB3_DRD_CON(usb3));
			usb3_drd_clear_bit(usb3, DRD_CON_HOST_RST, USB3_DRD_CON(usb3));
		} else {
			usb3_drd_set_bit(usb3, DRD_CON_HOST_RST, USB3_DRD_CON(usb3));
			usb3_drd_clear_bit(usb3, DRD_CON_PERI_RST, USB3_DRD_CON(usb3));
		}
	}

	if (host)
		usb3_drd_clear_bit(usb3, DRD_CON_PERI_CON, USB3_DRD_CON(usb3));
	else
		usb3_drd_set_bit(usb3, DRD_CON_PERI_CON, USB3_DRD_CON(usb3));
}

static void usb3_set_mode_by_role_sw(struct renesas_usb3 *usb3, bool host)
{
	if (usb3->role_sw) {
		usb3->role = host ? USB_ROLE_HOST : USB_ROLE_DEVICE;
		schedule_work(&usb3->role_work);
	} else {
		usb3_set_mode(usb3, host);
	}
}

static void usb3_vbus_out(struct renesas_usb3 *usb3, bool enable)
{
	if (enable)
		usb3_drd_set_bit(usb3, DRD_CON_VBOUT, USB3_DRD_CON(usb3));
	else
		usb3_drd_clear_bit(usb3, DRD_CON_VBOUT, USB3_DRD_CON(usb3));
}

static void usb3_mode_config(struct renesas_usb3 *usb3, bool host, bool a_dev)
{
	unsigned long flags;

	spin_lock_irqsave(&usb3->lock, flags);
	if (!usb3->role_sw_by_connector ||
	    usb3->connection_state != USB_ROLE_NONE) {
		usb3_set_mode_by_role_sw(usb3, host);
		usb3_vbus_out(usb3, a_dev);
	}
	/* for A-Peripheral or forced B-device mode */
	if ((!host && a_dev) || usb3->start_to_connect)
		usb3_connect(usb3);
	spin_unlock_irqrestore(&usb3->lock, flags);
}

static bool usb3_is_a_device(struct renesas_usb3 *usb3)
{
	return !(usb3_drd_read(usb3, USB3_USB_OTG_STA(usb3)) & USB_OTG_IDMON(usb3));
}

static void usb3_check_id(struct renesas_usb3 *usb3)
{
	usb3->extcon_host = usb3_is_a_device(usb3);

	if ((!usb3->role_sw_by_connector && usb3->extcon_host &&
	     !usb3->forced_b_device) || usb3->connection_state == USB_ROLE_HOST)
		usb3_mode_config(usb3, true, true);
	else
		usb3_mode_config(usb3, false, false);

	schedule_work(&usb3->extcon_work);
}

static void renesas_usb3_init_controller(struct renesas_usb3 *usb3)
{
	usb3_init_axi_bridge(usb3);
	usb3_init_epc_registers(usb3);
	usb3_set_bit(usb3, USB_COM_CON_PN_WDATAIF_NL |
		     USB_COM_CON_PN_RDATAIF_NL | USB_COM_CON_PN_LSTTR_PP,
		     USB3_USB_COM_CON);
	usb3_drd_write(usb3, USB_OTG_IDMON(usb3), USB3_USB_OTG_INT_STA(usb3));
	usb3_drd_write(usb3, USB_OTG_IDMON(usb3), USB3_USB_OTG_INT_ENA(usb3));

	usb3_check_id(usb3);
	usb3_check_vbus(usb3);
}

static void renesas_usb3_stop_controller(struct renesas_usb3 *usb3)
{
	usb3_disconnect(usb3);
	usb3_write(usb3, 0, USB3_P0_INT_ENA);
	usb3_drd_write(usb3, 0, USB3_USB_OTG_INT_ENA(usb3));
	usb3_write(usb3, 0, USB3_USB_INT_ENA_1);
	usb3_write(usb3, 0, USB3_USB_INT_ENA_2);
	usb3_write(usb3, 0, USB3_AXI_INT_ENA);
}

static void usb3_irq_epc_int_1_pll_wakeup(struct renesas_usb3 *usb3)
{
	usb3_disable_irq_1(usb3, USB_INT_1_B3_PLLWKUP);
	usb3_clear_bit(usb3, USB30_CON_B3_PLLWAKE, USB3_USB30_CON);
	usb3_start_usb3_connection(usb3);
}

static void usb3_irq_epc_int_1_linkup_success(struct renesas_usb3 *usb3)
{
	usb3_transition_to_default_state(usb3, true);
}

static void usb3_irq_epc_int_1_resume(struct renesas_usb3 *usb3)
{
	usb3_disable_irq_1(usb3, USB_INT_1_B2_RSUM);
	usb3_start_usb2_connection(usb3);
	usb3_transition_to_default_state(usb3, false);
}

static void usb3_irq_epc_int_1_suspend(struct renesas_usb3 *usb3)
{
	usb3_disable_irq_1(usb3, USB_INT_1_B2_SPND);

	if (usb3->gadget.speed != USB_SPEED_UNKNOWN &&
	    usb3->gadget.state != USB_STATE_NOTATTACHED) {
		if (usb3->driver && usb3->driver->suspend)
			usb3->driver->suspend(&usb3->gadget);
		usb_gadget_set_state(&usb3->gadget, USB_STATE_SUSPENDED);
	}
}

static void usb3_irq_epc_int_1_disable(struct renesas_usb3 *usb3)
{
	usb3_stop_usb3_connection(usb3);
	if (usb3_wakeup_usb2_phy(usb3))
		usb3_irq_epc_int_1_resume(usb3);
}

static void usb3_irq_epc_int_1_bus_reset(struct renesas_usb3 *usb3)
{
	usb3_reset_epc(usb3);
	if (usb3->disabled_count < 3)
		usb3_start_usb3_connection(usb3);
	else
		usb3_start_usb2_connection(usb3);
}

static void usb3_irq_epc_int_1_vbus_change(struct renesas_usb3 *usb3)
{
	usb3_check_vbus(usb3);
}

static void usb3_irq_epc_int_1_hot_reset(struct renesas_usb3 *usb3)
{
	usb3_reset_epc(usb3);
	usb3_set_bit(usb3, USB_COM_CON_EP0_EN, USB3_USB_COM_CON);

	/* This bit shall be set within 12ms from the start of HotReset */
	usb3_set_bit(usb3, USB30_CON_B3_HOTRST_CMP, USB3_USB30_CON);
}

static void usb3_irq_epc_int_1_warm_reset(struct renesas_usb3 *usb3)
{
	usb3_reset_epc(usb3);
	usb3_set_bit(usb3, USB_COM_CON_EP0_EN, USB3_USB_COM_CON);

	usb3_start_operation_for_usb3(usb3);
	usb3_enable_irq_1(usb3, USB_INT_1_SPEED);
}

static void usb3_irq_epc_int_1_speed(struct renesas_usb3 *usb3)
{
	u32 speed = usb3_read(usb3, USB3_USB_STA) & USB_STA_SPEED_MASK;

	switch (speed) {
	case USB_STA_SPEED_SS:
		usb3->gadget.speed = USB_SPEED_SUPER;
		usb3->gadget.ep0->maxpacket = USB3_EP0_SS_MAX_PACKET_SIZE;
		break;
	case USB_STA_SPEED_HS:
		usb3->gadget.speed = USB_SPEED_HIGH;
		usb3->gadget.ep0->maxpacket = USB3_EP0_HSFS_MAX_PACKET_SIZE;
		break;
	case USB_STA_SPEED_FS:
		usb3->gadget.speed = USB_SPEED_FULL;
		usb3->gadget.ep0->maxpacket = USB3_EP0_HSFS_MAX_PACKET_SIZE;
		break;
	default:
		usb3->gadget.speed = USB_SPEED_UNKNOWN;
		break;
	}
}

static void usb3_irq_epc_int_1(struct renesas_usb3 *usb3, u32 int_sta_1)
{
	if (int_sta_1 & USB_INT_1_B3_PLLWKUP)
		usb3_irq_epc_int_1_pll_wakeup(usb3);

	if (int_sta_1 & USB_INT_1_B3_LUPSUCS)
		usb3_irq_epc_int_1_linkup_success(usb3);

	if (int_sta_1 & USB_INT_1_B3_HOTRST)
		usb3_irq_epc_int_1_hot_reset(usb3);

	if (int_sta_1 & USB_INT_1_B3_WRMRST)
		usb3_irq_epc_int_1_warm_reset(usb3);

	if (int_sta_1 & USB_INT_1_B3_DISABLE)
		usb3_irq_epc_int_1_disable(usb3);

	if (int_sta_1 & USB_INT_1_B2_USBRST)
		usb3_irq_epc_int_1_bus_reset(usb3);

	if (int_sta_1 & USB_INT_1_B2_RSUM)
		usb3_irq_epc_int_1_resume(usb3);

	if (int_sta_1 & USB_INT_1_B2_SPND)
		usb3_irq_epc_int_1_suspend(usb3);

	if (int_sta_1 & USB_INT_1_SPEED)
		usb3_irq_epc_int_1_speed(usb3);

	if (int_sta_1 & USB_INT_1_VBUS_CNG)
		usb3_irq_epc_int_1_vbus_change(usb3);
}

static struct renesas_usb3_request *__usb3_get_request(struct renesas_usb3_ep
						       *usb3_ep)
{
	return list_first_entry_or_null(&usb3_ep->queue,
					struct renesas_usb3_request, queue);
}

static struct renesas_usb3_request *usb3_get_request(struct renesas_usb3_ep
						     *usb3_ep)
{
	struct renesas_usb3 *usb3 = usb3_ep_to_usb3(usb3_ep);
	struct renesas_usb3_request *usb3_req;
	unsigned long flags;

	spin_lock_irqsave(&usb3->lock, flags);
	usb3_req = __usb3_get_request(usb3_ep);
	spin_unlock_irqrestore(&usb3->lock, flags);

	return usb3_req;
}

static void __usb3_request_done(struct renesas_usb3_ep *usb3_ep,
				struct renesas_usb3_request *usb3_req,
				int status)
{
	struct renesas_usb3 *usb3 = usb3_ep_to_usb3(usb3_ep);

	dev_dbg(usb3_to_dev(usb3), "giveback: ep%2d, %u, %u, %d\n",
		usb3_ep->num, usb3_req->req.length, usb3_req->req.actual,
		status);
	usb3_req->req.status = status;
	usb3_ep->started = false;
	list_del_init(&usb3_req->queue);
	spin_unlock(&usb3->lock);
	usb_gadget_giveback_request(&usb3_ep->ep, &usb3_req->req);
	spin_lock(&usb3->lock);
}

static void usb3_request_done(struct renesas_usb3_ep *usb3_ep,
			      struct renesas_usb3_request *usb3_req, int status)
{
	struct renesas_usb3 *usb3 = usb3_ep_to_usb3(usb3_ep);
	unsigned long flags;

	spin_lock_irqsave(&usb3->lock, flags);
	__usb3_request_done(usb3_ep, usb3_req, status);
	spin_unlock_irqrestore(&usb3->lock, flags);
}

static void usb3_irq_epc_pipe0_status_end(struct renesas_usb3 *usb3)
{
	struct renesas_usb3_ep *usb3_ep = usb3_get_ep(usb3, 0);
	struct renesas_usb3_request *usb3_req = usb3_get_request(usb3_ep);

	if (usb3_req)
		usb3_request_done(usb3_ep, usb3_req, 0);
	if (usb3->test_mode)
		usb3_set_test_mode(usb3);
}

static void usb3_get_setup_data(struct renesas_usb3 *usb3,
				struct usb_ctrlrequest *ctrl)
{
	struct renesas_usb3_ep *usb3_ep = usb3_get_ep(usb3, 0);
	u32 *data = (u32 *)ctrl;

	*data++ = usb3_read(usb3, USB3_STUP_DAT_0);
	*data = usb3_read(usb3, USB3_STUP_DAT_1);

	/* update this driver's flag */
	usb3_ep->dir_in = !!(ctrl->bRequestType & USB_DIR_IN);
}

static void usb3_set_p0_con_update_res(struct renesas_usb3 *usb3, u32 res)
{
	u32 val = usb3_read(usb3, USB3_P0_CON);

	val &= ~(P0_CON_ST_RES_MASK | P0_CON_OT_RES_MASK | P0_CON_IN_RES_MASK);
	val |= res | P0_CON_RES_WEN;
	usb3_write(usb3, val, USB3_P0_CON);
}

static void usb3_set_p0_con_for_ctrl_read_data(struct renesas_usb3 *usb3)
{
	usb3_set_p0_con_update_res(usb3, P0_CON_ST_RES_FORCE_NRDY |
				   P0_CON_OT_RES_FORCE_STALL |
				   P0_CON_IN_RES_NORMAL);
}

static void usb3_set_p0_con_for_ctrl_read_status(struct renesas_usb3 *usb3)
{
	usb3_set_p0_con_update_res(usb3, P0_CON_ST_RES_NORMAL |
				   P0_CON_OT_RES_FORCE_STALL |
				   P0_CON_IN_RES_NORMAL);
}

static void usb3_set_p0_con_for_ctrl_write_data(struct renesas_usb3 *usb3)
{
	usb3_set_p0_con_update_res(usb3, P0_CON_ST_RES_FORCE_NRDY |
				   P0_CON_OT_RES_NORMAL |
				   P0_CON_IN_RES_FORCE_STALL);
}

static void usb3_set_p0_con_for_ctrl_write_status(struct renesas_usb3 *usb3)
{
	usb3_set_p0_con_update_res(usb3, P0_CON_ST_RES_NORMAL |
				   P0_CON_OT_RES_NORMAL |
				   P0_CON_IN_RES_FORCE_STALL);
}

static void usb3_set_p0_con_for_no_data(struct renesas_usb3 *usb3)
{
	usb3_set_p0_con_update_res(usb3, P0_CON_ST_RES_NORMAL |
				   P0_CON_OT_RES_FORCE_STALL |
				   P0_CON_IN_RES_FORCE_STALL);
}

static void usb3_set_p0_con_stall(struct renesas_usb3 *usb3)
{
	usb3_set_p0_con_update_res(usb3, P0_CON_ST_RES_FORCE_STALL |
				   P0_CON_OT_RES_FORCE_STALL |
				   P0_CON_IN_RES_FORCE_STALL);
}

static void usb3_set_p0_con_stop(struct renesas_usb3 *usb3)
{
	usb3_set_p0_con_update_res(usb3, P0_CON_ST_RES_FORCE_NRDY |
				   P0_CON_OT_RES_FORCE_NRDY |
				   P0_CON_IN_RES_FORCE_NRDY);
}

static int usb3_pn_change(struct renesas_usb3 *usb3, int num)
{
	if (num == 0 || num > usb3->num_usb3_eps)
		return -ENXIO;

	usb3_write(usb3, num, USB3_PIPE_COM);

	return 0;
}

static void usb3_set_pn_con_update_res(struct renesas_usb3 *usb3, u32 res)
{
	u32 val = usb3_read(usb3, USB3_PN_CON);

	val &= ~PN_CON_RES_MASK;
	val |= res & PN_CON_RES_MASK;
	val |= PN_CON_RES_WEN;
	usb3_write(usb3, val, USB3_PN_CON);
}

static void usb3_pn_start(struct renesas_usb3 *usb3)
{
	usb3_set_pn_con_update_res(usb3, PN_CON_RES_NORMAL);
}

static void usb3_pn_stop(struct renesas_usb3 *usb3)
{
	usb3_set_pn_con_update_res(usb3, PN_CON_RES_FORCE_NRDY);
}

static void usb3_pn_stall(struct renesas_usb3 *usb3)
{
	usb3_set_pn_con_update_res(usb3, PN_CON_RES_FORCE_STALL);
}

static int usb3_pn_con_clear(struct renesas_usb3 *usb3)
{
	usb3_set_bit(usb3, PN_CON_CLR, USB3_PN_CON);

	return usb3_wait(usb3, USB3_PN_CON, PN_CON_CLR, 0);
}

static bool usb3_is_transfer_complete(struct renesas_usb3_ep *usb3_ep,
				      struct renesas_usb3_request *usb3_req)
{
	struct usb_request *req = &usb3_req->req;

	if ((!req->zero && req->actual == req->length) ||
	    (req->actual % usb3_ep->ep.maxpacket) || (req->length == 0))
		return true;
	else
		return false;
}

static int usb3_wait_pipe_status(struct renesas_usb3_ep *usb3_ep, u32 mask)
{
	struct renesas_usb3 *usb3 = usb3_ep_to_usb3(usb3_ep);
	u32 sta_reg = usb3_ep->num ? USB3_PN_STA : USB3_P0_STA;

	return usb3_wait(usb3, sta_reg, mask, mask);
}

static void usb3_set_px_con_send(struct renesas_usb3_ep *usb3_ep, int bytes,
				 bool last)
{
	struct renesas_usb3 *usb3 = usb3_ep_to_usb3(usb3_ep);
	u32 con_reg = usb3_ep->num ? USB3_PN_CON : USB3_P0_CON;
	u32 val = usb3_read(usb3, con_reg);

	val |= PX_CON_SEND | PX_CON_BYTE_EN_BYTES(bytes);
	val |= (usb3_ep->num && last) ? PN_CON_LAST : 0;
	usb3_write(usb3, val, con_reg);
}

static int usb3_write_pipe(struct renesas_usb3_ep *usb3_ep,
			   struct renesas_usb3_request *usb3_req,
			   u32 fifo_reg)
{
	struct renesas_usb3 *usb3 = usb3_ep_to_usb3(usb3_ep);
	int i;
	int len = min_t(unsigned, usb3_req->req.length - usb3_req->req.actual,
			usb3_ep->ep.maxpacket);
	u8 *buf = usb3_req->req.buf + usb3_req->req.actual;
	u32 tmp = 0;
	bool is_last = !len ? true : false;

	if (usb3_wait_pipe_status(usb3_ep, PX_STA_BUFSTS) < 0)
		return -EBUSY;

	/* Update gadget driver parameter */
	usb3_req->req.actual += len;

	/* Write data to the register */
	if (len >= 4) {
		iowrite32_rep(usb3->reg + fifo_reg, buf, len / 4);
		buf += (len / 4) * 4;
		len %= 4;	/* update len to use usb3_set_pX_con_send() */
	}

	if (len) {
		for (i = 0; i < len; i++)
			tmp |= buf[i] << (8 * i);
		usb3_write(usb3, tmp, fifo_reg);
	}

	if (!is_last)
		is_last = usb3_is_transfer_complete(usb3_ep, usb3_req);
	/* Send the data */
	usb3_set_px_con_send(usb3_ep, len, is_last);

	return is_last ? 0 : -EAGAIN;
}

static u32 usb3_get_received_length(struct renesas_usb3_ep *usb3_ep)
{
	struct renesas_usb3 *usb3 = usb3_ep_to_usb3(usb3_ep);
	u32 lng_reg = usb3_ep->num ? USB3_PN_LNG : USB3_P0_LNG;

	return usb3_read(usb3, lng_reg);
}

static int usb3_read_pipe(struct renesas_usb3_ep *usb3_ep,
			  struct renesas_usb3_request *usb3_req, u32 fifo_reg)
{
	struct renesas_usb3 *usb3 = usb3_ep_to_usb3(usb3_ep);
	int i;
	int len = min_t(unsigned, usb3_req->req.length - usb3_req->req.actual,
			usb3_get_received_length(usb3_ep));
	u8 *buf = usb3_req->req.buf + usb3_req->req.actual;
	u32 tmp = 0;

	if (!len)
		return 0;

	/* Update gadget driver parameter */
	usb3_req->req.actual += len;

	/* Read data from the register */
	if (len >= 4) {
		ioread32_rep(usb3->reg + fifo_reg, buf, len / 4);
		buf += (len / 4) * 4;
		len %= 4;
	}

	if (len) {
		tmp = usb3_read(usb3, fifo_reg);
		for (i = 0; i < len; i++)
			buf[i] = (tmp >> (8 * i)) & 0xff;
	}

	return usb3_is_transfer_complete(usb3_ep, usb3_req) ? 0 : -EAGAIN;
}

static void usb3_set_status_stage(struct renesas_usb3_ep *usb3_ep,
				  struct renesas_usb3_request *usb3_req)
{
	struct renesas_usb3 *usb3 = usb3_ep_to_usb3(usb3_ep);

	if (usb3_ep->dir_in) {
		usb3_set_p0_con_for_ctrl_read_status(usb3);
	} else {
		if (!usb3_req->req.length)
			usb3_set_p0_con_for_no_data(usb3);
		else
			usb3_set_p0_con_for_ctrl_write_status(usb3);
	}
}

static void usb3_p0_xfer(struct renesas_usb3_ep *usb3_ep,
			 struct renesas_usb3_request *usb3_req)
{
	int ret;

	if (usb3_ep->dir_in)
		ret = usb3_write_pipe(usb3_ep, usb3_req, USB3_P0_WRITE);
	else
		ret = usb3_read_pipe(usb3_ep, usb3_req, USB3_P0_READ);

	if (!ret)
		usb3_set_status_stage(usb3_ep, usb3_req);
}

static void usb3_start_pipe0(struct renesas_usb3_ep *usb3_ep,
			     struct renesas_usb3_request *usb3_req)
{
	struct renesas_usb3 *usb3 = usb3_ep_to_usb3(usb3_ep);

	if (usb3_ep->started)
		return;

	usb3_ep->started = true;

	if (usb3_ep->dir_in) {
		usb3_set_bit(usb3, P0_MOD_DIR, USB3_P0_MOD);
		usb3_set_p0_con_for_ctrl_read_data(usb3);
	} else {
		usb3_clear_bit(usb3, P0_MOD_DIR, USB3_P0_MOD);
		if (usb3_req->req.length)
			usb3_set_p0_con_for_ctrl_write_data(usb3);
	}

	usb3_p0_xfer(usb3_ep, usb3_req);
}

static void usb3_enable_dma_pipen(struct renesas_usb3 *usb3)
{
	usb3_set_bit(usb3, PN_CON_DATAIF_EN, USB3_PN_CON);
}

static void usb3_disable_dma_pipen(struct renesas_usb3 *usb3)
{
	usb3_clear_bit(usb3, PN_CON_DATAIF_EN, USB3_PN_CON);
}

static void usb3_enable_dma_irq(struct renesas_usb3 *usb3, int num)
{
	usb3_set_bit(usb3, DMA_INT(num), USB3_DMA_INT_ENA);
}

static void usb3_disable_dma_irq(struct renesas_usb3 *usb3, int num)
{
	usb3_clear_bit(usb3, DMA_INT(num), USB3_DMA_INT_ENA);
}

static u32 usb3_dma_mps_to_prd_word1(struct renesas_usb3_ep *usb3_ep)
{
	switch (usb3_ep->ep.maxpacket) {
	case 8:
		return USB3_PRD1_MPS_8;
	case 16:
		return USB3_PRD1_MPS_16;
	case 32:
		return USB3_PRD1_MPS_32;
	case 64:
		return USB3_PRD1_MPS_64;
	case 512:
		return USB3_PRD1_MPS_512;
	case 1024:
		return USB3_PRD1_MPS_1024;
	default:
		return USB3_PRD1_MPS_RESERVED;
	}
}

static bool usb3_dma_get_setting_area(struct renesas_usb3_ep *usb3_ep,
				      struct renesas_usb3_request *usb3_req)
{
	struct renesas_usb3 *usb3 = usb3_ep_to_usb3(usb3_ep);
	struct renesas_usb3_dma *dma;
	int i;
	bool ret = false;

	if (usb3_req->req.length > USB3_DMA_MAX_XFER_SIZE_ALL_PRDS) {
		dev_dbg(usb3_to_dev(usb3), "%s: the length is too big (%d)\n",
			__func__, usb3_req->req.length);
		return false;
	}

	/* The driver doesn't handle zero-length packet via dmac */
	if (!usb3_req->req.length)
		return false;

	if (usb3_dma_mps_to_prd_word1(usb3_ep) == USB3_PRD1_MPS_RESERVED)
		return false;

	usb3_for_each_dma(usb3, dma, i) {
		if (dma->used)
			continue;

		if (usb_gadget_map_request(&usb3->gadget, &usb3_req->req,
					   usb3_ep->dir_in) < 0)
			break;

		dma->used = true;
		usb3_ep->dma = dma;
		ret = true;
		break;
	}

	return ret;
}

static void usb3_dma_put_setting_area(struct renesas_usb3_ep *usb3_ep,
				      struct renesas_usb3_request *usb3_req)
{
	struct renesas_usb3 *usb3 = usb3_ep_to_usb3(usb3_ep);
	int i;
	struct renesas_usb3_dma *dma;

	usb3_for_each_dma(usb3, dma, i) {
		if (usb3_ep->dma == dma) {
			usb_gadget_unmap_request(&usb3->gadget, &usb3_req->req,
						 usb3_ep->dir_in);
			dma->used = false;
			usb3_ep->dma = NULL;
			break;
		}
	}
}

static void usb3_dma_fill_prd(struct renesas_usb3_ep *usb3_ep,
			      struct renesas_usb3_request *usb3_req)
{
	struct renesas_usb3_prd *cur_prd = usb3_ep->dma->prd;
	u32 remain = usb3_req->req.length;
	u32 dma = usb3_req->req.dma;
	u32 len;
	int i = 0;

	do {
		len = min_t(u32, remain, USB3_DMA_MAX_XFER_SIZE) &
			    USB3_PRD1_SIZE_MASK;
		cur_prd->word1 = usb3_dma_mps_to_prd_word1(usb3_ep) |
				 USB3_PRD1_B_INC | len;
		cur_prd->bap = dma;
		remain -= len;
		dma += len;
		if (!remain || (i + 1) < USB3_DMA_NUM_PRD_ENTRIES)
			break;

		cur_prd++;
		i++;
	} while (1);

	cur_prd->word1 |= USB3_PRD1_E | USB3_PRD1_INT;
	if (usb3_ep->dir_in)
		cur_prd->word1 |= USB3_PRD1_LST;
}

static void usb3_dma_kick_prd(struct renesas_usb3_ep *usb3_ep)
{
	struct renesas_usb3_dma *dma = usb3_ep->dma;
	struct renesas_usb3 *usb3 = usb3_ep_to_usb3(usb3_ep);
	u32 dma_con = DMA_COM_PIPE_NO(usb3_ep->num) | DMA_CON_PRD_EN;

	if (usb3_ep->dir_in)
		dma_con |= DMA_CON_PIPE_DIR;

	wmb();	/* prd entries should be in system memory here */

	usb3_write(usb3, 1 << usb3_ep->num, USB3_DMA_INT_STA);
	usb3_write(usb3, AXI_INT_PRDEN_CLR_STA(dma->num) |
		   AXI_INT_PRDERR_STA(dma->num), USB3_AXI_INT_STA);

	usb3_write(usb3, dma->prd_dma, USB3_DMA_CH0_PRD_ADR(dma->num));
	usb3_write(usb3, dma_con, USB3_DMA_CH0_CON(dma->num));
	usb3_enable_dma_irq(usb3, usb3_ep->num);
}

static void usb3_dma_stop_prd(struct renesas_usb3_ep *usb3_ep)
{
	struct renesas_usb3 *usb3 = usb3_ep_to_usb3(usb3_ep);
	struct renesas_usb3_dma *dma = usb3_ep->dma;

	usb3_disable_dma_irq(usb3, usb3_ep->num);
	usb3_write(usb3, 0, USB3_DMA_CH0_CON(dma->num));
}

static int usb3_dma_update_status(struct renesas_usb3_ep *usb3_ep,
				  struct renesas_usb3_request *usb3_req)
{
	struct renesas_usb3_prd *cur_prd = usb3_ep->dma->prd;
	struct usb_request *req = &usb3_req->req;
	u32 remain, len;
	int i = 0;
	int status = 0;

	rmb();	/* The controller updated prd entries */

	do {
		if (cur_prd->word1 & USB3_PRD1_D)
			status = -EIO;
		if (cur_prd->word1 & USB3_PRD1_E)
			len = req->length % USB3_DMA_MAX_XFER_SIZE;
		else
			len = USB3_DMA_MAX_XFER_SIZE;
		remain = cur_prd->word1 & USB3_PRD1_SIZE_MASK;
		req->actual += len - remain;

		if (cur_prd->word1 & USB3_PRD1_E ||
		    (i + 1) < USB3_DMA_NUM_PRD_ENTRIES)
			break;

		cur_prd++;
		i++;
	} while (1);

	return status;
}

static bool usb3_dma_try_start(struct renesas_usb3_ep *usb3_ep,
			       struct renesas_usb3_request *usb3_req)
{
	struct renesas_usb3 *usb3 = usb3_ep_to_usb3(usb3_ep);

	if (!use_dma)
		return false;

	if (usb3_dma_get_setting_area(usb3_ep, usb3_req)) {
		usb3_pn_stop(usb3);
		usb3_enable_dma_pipen(usb3);
		usb3_dma_fill_prd(usb3_ep, usb3_req);
		usb3_dma_kick_prd(usb3_ep);
		usb3_pn_start(usb3);
		return true;
	}

	return false;
}

static int usb3_dma_try_stop(struct renesas_usb3_ep *usb3_ep,
			     struct renesas_usb3_request *usb3_req)
{
	struct renesas_usb3 *usb3 = usb3_ep_to_usb3(usb3_ep);
	unsigned long flags;
	int status = 0;

	spin_lock_irqsave(&usb3->lock, flags);
	if (!usb3_ep->dma)
		goto out;

	if (!usb3_pn_change(usb3, usb3_ep->num))
		usb3_disable_dma_pipen(usb3);
	usb3_dma_stop_prd(usb3_ep);
	status = usb3_dma_update_status(usb3_ep, usb3_req);
	usb3_dma_put_setting_area(usb3_ep, usb3_req);

out:
	spin_unlock_irqrestore(&usb3->lock, flags);
	return status;
}

static int renesas_usb3_dma_free_prd(struct renesas_usb3 *usb3,
				     struct device *dev)
{
	int i;
	struct renesas_usb3_dma *dma;

	usb3_for_each_dma(usb3, dma, i) {
		if (dma->prd) {
			dma_free_coherent(dev, USB3_DMA_PRD_SIZE,
					  dma->prd, dma->prd_dma);
			dma->prd = NULL;
		}
	}

	return 0;
}

static int renesas_usb3_dma_alloc_prd(struct renesas_usb3 *usb3,
				      struct device *dev)
{
	int i;
	struct renesas_usb3_dma *dma;

	if (!use_dma)
		return 0;

	usb3_for_each_dma(usb3, dma, i) {
		dma->prd = dma_alloc_coherent(dev, USB3_DMA_PRD_SIZE,
					      &dma->prd_dma, GFP_KERNEL);
		if (!dma->prd) {
			renesas_usb3_dma_free_prd(usb3, dev);
			return -ENOMEM;
		}
		dma->num = i + 1;
	}

	return 0;
}

static void usb3_start_pipen(struct renesas_usb3_ep *usb3_ep,
			     struct renesas_usb3_request *usb3_req)
{
	struct renesas_usb3 *usb3 = usb3_ep_to_usb3(usb3_ep);
	struct renesas_usb3_request *usb3_req_first;
	unsigned long flags;
	int ret = -EAGAIN;
	u32 enable_bits = 0;

	spin_lock_irqsave(&usb3->lock, flags);
	if (usb3_ep->halt || usb3_ep->started)
		goto out;
	usb3_req_first = __usb3_get_request(usb3_ep);
	if (!usb3_req_first || usb3_req != usb3_req_first)
		goto out;

	if (usb3_pn_change(usb3, usb3_ep->num) < 0)
		goto out;

	usb3_ep->started = true;

	if (usb3_dma_try_start(usb3_ep, usb3_req))
		goto out;

	usb3_pn_start(usb3);

	if (usb3_ep->dir_in) {
		ret = usb3_write_pipe(usb3_ep, usb3_req, USB3_PN_WRITE);
		enable_bits |= PN_INT_LSTTR;
	}

	if (ret < 0)
		enable_bits |= PN_INT_BFRDY;

	if (enable_bits) {
		usb3_set_bit(usb3, enable_bits, USB3_PN_INT_ENA);
		usb3_enable_pipe_irq(usb3, usb3_ep->num);
	}
out:
	spin_unlock_irqrestore(&usb3->lock, flags);
}

static int renesas_usb3_ep_queue(struct usb_ep *_ep, struct usb_request *_req,
				 gfp_t gfp_flags)
{
	struct renesas_usb3_ep *usb3_ep = usb_ep_to_usb3_ep(_ep);
	struct renesas_usb3_request *usb3_req = usb_req_to_usb3_req(_req);
	struct renesas_usb3 *usb3 = usb3_ep_to_usb3(usb3_ep);
	unsigned long flags;

	dev_dbg(usb3_to_dev(usb3), "ep_queue: ep%2d, %u\n", usb3_ep->num,
		_req->length);

	_req->status = -EINPROGRESS;
	_req->actual = 0;
	spin_lock_irqsave(&usb3->lock, flags);
	list_add_tail(&usb3_req->queue, &usb3_ep->queue);
	spin_unlock_irqrestore(&usb3->lock, flags);

	if (!usb3_ep->num)
		usb3_start_pipe0(usb3_ep, usb3_req);
	else
		usb3_start_pipen(usb3_ep, usb3_req);

	return 0;
}

static void usb3_set_device_address(struct renesas_usb3 *usb3, u16 addr)
{
	/* DEV_ADDR bit field is cleared by WarmReset, HotReset and BusReset */
	usb3_set_bit(usb3, USB_COM_CON_DEV_ADDR(addr), USB3_USB_COM_CON);
}

static bool usb3_std_req_set_address(struct renesas_usb3 *usb3,
				     struct usb_ctrlrequest *ctrl)
{
	if (le16_to_cpu(ctrl->wValue) >= 128)
		return true;	/* stall */

	usb3_set_device_address(usb3, le16_to_cpu(ctrl->wValue));
	usb3_set_p0_con_for_no_data(usb3);

	return false;
}

static void usb3_pipe0_internal_xfer(struct renesas_usb3 *usb3,
				     void *tx_data, size_t len,
				     void (*complete)(struct usb_ep *ep,
						      struct usb_request *req))
{
	struct renesas_usb3_ep *usb3_ep = usb3_get_ep(usb3, 0);

	if (tx_data)
		memcpy(usb3->ep0_buf, tx_data,
		       min_t(size_t, len, USB3_EP0_BUF_SIZE));

	usb3->ep0_req->buf = &usb3->ep0_buf;
	usb3->ep0_req->length = len;
	usb3->ep0_req->complete = complete;
	renesas_usb3_ep_queue(&usb3_ep->ep, usb3->ep0_req, GFP_ATOMIC);
}

static void usb3_pipe0_get_status_completion(struct usb_ep *ep,
					     struct usb_request *req)
{
}

static bool usb3_std_req_get_status(struct renesas_usb3 *usb3,
				    struct usb_ctrlrequest *ctrl)
{
	bool stall = false;
	struct renesas_usb3_ep *usb3_ep;
	int num;
	u16 status = 0;
	__le16 tx_data;

	switch (ctrl->bRequestType & USB_RECIP_MASK) {
	case USB_RECIP_DEVICE:
		if (usb3->gadget.is_selfpowered)
			status |= 1 << USB_DEVICE_SELF_POWERED;
		if (usb3->gadget.speed == USB_SPEED_SUPER)
			status |= usb3_feature_get_un_enabled(usb3);
		break;
	case USB_RECIP_INTERFACE:
		break;
	case USB_RECIP_ENDPOINT:
		num = le16_to_cpu(ctrl->wIndex) & USB_ENDPOINT_NUMBER_MASK;
		usb3_ep = usb3_get_ep(usb3, num);
		if (usb3_ep->halt)
			status |= 1 << USB_ENDPOINT_HALT;
		break;
	default:
		stall = true;
		break;
	}

	if (!stall) {
		tx_data = cpu_to_le16(status);
		dev_dbg(usb3_to_dev(usb3), "get_status: req = %p\n",
			usb_req_to_usb3_req(usb3->ep0_req));
		usb3_pipe0_internal_xfer(usb3, &tx_data, sizeof(tx_data),
					 usb3_pipe0_get_status_completion);
	}

	return stall;
}

static bool usb3_std_req_feature_device(struct renesas_usb3 *usb3,
					struct usb_ctrlrequest *ctrl, bool set)
{
	bool stall = true;
	u16 w_value = le16_to_cpu(ctrl->wValue);

	switch (w_value) {
	case USB_DEVICE_TEST_MODE:
		if (!set)
			break;
		usb3->test_mode = le16_to_cpu(ctrl->wIndex) >> 8;
		stall = false;
		break;
	case USB_DEVICE_U1_ENABLE:
	case USB_DEVICE_U2_ENABLE:
		if (usb3->gadget.speed != USB_SPEED_SUPER)
			break;
		if (w_value == USB_DEVICE_U1_ENABLE)
			usb3_feature_u1_enable(usb3, set);
		if (w_value == USB_DEVICE_U2_ENABLE)
			usb3_feature_u2_enable(usb3, set);
		stall = false;
		break;
	default:
		break;
	}

	return stall;
}

static int usb3_set_halt_p0(struct renesas_usb3_ep *usb3_ep, bool halt)
{
	struct renesas_usb3 *usb3 = usb3_ep_to_usb3(usb3_ep);

	if (unlikely(usb3_ep->num))
		return -EINVAL;

	usb3_ep->halt = halt;
	if (halt)
		usb3_set_p0_con_stall(usb3);
	else
		usb3_set_p0_con_stop(usb3);

	return 0;
}

static int usb3_set_halt_pn(struct renesas_usb3_ep *usb3_ep, bool halt,
			    bool is_clear_feature)
{
	struct renesas_usb3 *usb3 = usb3_ep_to_usb3(usb3_ep);
	unsigned long flags;

	spin_lock_irqsave(&usb3->lock, flags);
	if (!usb3_pn_change(usb3, usb3_ep->num)) {
		usb3_ep->halt = halt;
		if (halt) {
			usb3_pn_stall(usb3);
		} else if (!is_clear_feature || !usb3_ep->wedge) {
			usb3_pn_con_clear(usb3);
			usb3_set_bit(usb3, PN_CON_EN, USB3_PN_CON);
			usb3_pn_stop(usb3);
		}
	}
	spin_unlock_irqrestore(&usb3->lock, flags);

	return 0;
}

static int usb3_set_halt(struct renesas_usb3_ep *usb3_ep, bool halt,
			 bool is_clear_feature)
{
	int ret = 0;

	if (halt && usb3_ep->started)
		return -EAGAIN;

	if (usb3_ep->num)
		ret = usb3_set_halt_pn(usb3_ep, halt, is_clear_feature);
	else
		ret = usb3_set_halt_p0(usb3_ep, halt);

	return ret;
}

static bool usb3_std_req_feature_endpoint(struct renesas_usb3 *usb3,
					  struct usb_ctrlrequest *ctrl,
					  bool set)
{
	int num = le16_to_cpu(ctrl->wIndex) & USB_ENDPOINT_NUMBER_MASK;
	struct renesas_usb3_ep *usb3_ep;
	struct renesas_usb3_request *usb3_req;

	if (le16_to_cpu(ctrl->wValue) != USB_ENDPOINT_HALT)
		return true;	/* stall */

	usb3_ep = usb3_get_ep(usb3, num);
	usb3_set_halt(usb3_ep, set, true);

	/* Restarts a queue if clear feature */
	if (!set) {
		usb3_ep->started = false;
		usb3_req = usb3_get_request(usb3_ep);
		if (usb3_req)
			usb3_start_pipen(usb3_ep, usb3_req);
	}

	return false;
}

static bool usb3_std_req_feature(struct renesas_usb3 *usb3,
				 struct usb_ctrlrequest *ctrl, bool set)
{
	bool stall = false;

	switch (ctrl->bRequestType & USB_RECIP_MASK) {
	case USB_RECIP_DEVICE:
		stall = usb3_std_req_feature_device(usb3, ctrl, set);
		break;
	case USB_RECIP_INTERFACE:
		break;
	case USB_RECIP_ENDPOINT:
		stall = usb3_std_req_feature_endpoint(usb3, ctrl, set);
		break;
	default:
		stall = true;
		break;
	}

	if (!stall)
		usb3_set_p0_con_for_no_data(usb3);

	return stall;
}

static void usb3_pipe0_set_sel_completion(struct usb_ep *ep,
					  struct usb_request *req)
{
	/* TODO */
}

static bool usb3_std_req_set_sel(struct renesas_usb3 *usb3,
				 struct usb_ctrlrequest *ctrl)
{
	u16 w_length = le16_to_cpu(ctrl->wLength);

	if (w_length != 6)
		return true;	/* stall */

	dev_dbg(usb3_to_dev(usb3), "set_sel: req = %p\n",
		usb_req_to_usb3_req(usb3->ep0_req));
	usb3_pipe0_internal_xfer(usb3, NULL, 6, usb3_pipe0_set_sel_completion);

	return false;
}

static bool usb3_std_req_set_configuration(struct renesas_usb3 *usb3,
					   struct usb_ctrlrequest *ctrl)
{
	if (le16_to_cpu(ctrl->wValue) > 0)
		usb3_set_bit(usb3, USB_COM_CON_CONF, USB3_USB_COM_CON);
	else
		usb3_clear_bit(usb3, USB_COM_CON_CONF, USB3_USB_COM_CON);

	return false;
}

/**
 * usb3_handle_standard_request - handle some standard requests
 * @usb3: the renesas_usb3 pointer
 * @ctrl: a pointer of setup data
 *
 * Returns true if this function handled a standard request
 */
static bool usb3_handle_standard_request(struct renesas_usb3 *usb3,
					 struct usb_ctrlrequest *ctrl)
{
	bool ret = false;
	bool stall = false;

	if ((ctrl->bRequestType & USB_TYPE_MASK) == USB_TYPE_STANDARD) {
		switch (ctrl->bRequest) {
		case USB_REQ_SET_ADDRESS:
			stall = usb3_std_req_set_address(usb3, ctrl);
			ret = true;
			break;
		case USB_REQ_GET_STATUS:
			stall = usb3_std_req_get_status(usb3, ctrl);
			ret = true;
			break;
		case USB_REQ_CLEAR_FEATURE:
			stall = usb3_std_req_feature(usb3, ctrl, false);
			ret = true;
			break;
		case USB_REQ_SET_FEATURE:
			stall = usb3_std_req_feature(usb3, ctrl, true);
			ret = true;
			break;
		case USB_REQ_SET_SEL:
			stall = usb3_std_req_set_sel(usb3, ctrl);
			ret = true;
			break;
		case USB_REQ_SET_ISOCH_DELAY:
			/* This hardware doesn't support Isochronous xfer */
			stall = true;
			ret = true;
			break;
		case USB_REQ_SET_CONFIGURATION:
			usb3_std_req_set_configuration(usb3, ctrl);
			break;
		default:
			break;
		}
	}

	if (stall)
		usb3_set_p0_con_stall(usb3);

	return ret;
}

static int usb3_p0_con_clear_buffer(struct renesas_usb3 *usb3)
{
	usb3_set_bit(usb3, P0_CON_BCLR, USB3_P0_CON);

	return usb3_wait(usb3, USB3_P0_CON, P0_CON_BCLR, 0);
}

static void usb3_irq_epc_pipe0_setup(struct renesas_usb3 *usb3)
{
	struct usb_ctrlrequest ctrl;
	struct renesas_usb3_ep *usb3_ep = usb3_get_ep(usb3, 0);

	/* Call giveback function if previous transfer is not completed */
	if (usb3_ep->started)
		usb3_request_done(usb3_ep, usb3_get_request(usb3_ep),
				  -ECONNRESET);

	usb3_p0_con_clear_buffer(usb3);
	usb3_get_setup_data(usb3, &ctrl);
	if (!usb3_handle_standard_request(usb3, &ctrl))
		if (usb3->driver->setup(&usb3->gadget, &ctrl) < 0)
			usb3_set_p0_con_stall(usb3);
}

static void usb3_irq_epc_pipe0_bfrdy(struct renesas_usb3 *usb3)
{
	struct renesas_usb3_ep *usb3_ep = usb3_get_ep(usb3, 0);
	struct renesas_usb3_request *usb3_req = usb3_get_request(usb3_ep);

	if (!usb3_req)
		return;

	usb3_p0_xfer(usb3_ep, usb3_req);
}

static void usb3_irq_epc_pipe0(struct renesas_usb3 *usb3)
{
	u32 p0_int_sta = usb3_read(usb3, USB3_P0_INT_STA);

	p0_int_sta &= usb3_read(usb3, USB3_P0_INT_ENA);
	usb3_write(usb3, p0_int_sta, USB3_P0_INT_STA);
	if (p0_int_sta & P0_INT_STSED)
		usb3_irq_epc_pipe0_status_end(usb3);
	if (p0_int_sta & P0_INT_SETUP)
		usb3_irq_epc_pipe0_setup(usb3);
	if (p0_int_sta & P0_INT_BFRDY)
		usb3_irq_epc_pipe0_bfrdy(usb3);
}

static void usb3_request_done_pipen(struct renesas_usb3 *usb3,
				    struct renesas_usb3_ep *usb3_ep,
				    struct renesas_usb3_request *usb3_req,
				    int status)
{
	unsigned long flags;

	spin_lock_irqsave(&usb3->lock, flags);
	if (usb3_pn_change(usb3, usb3_ep->num))
		usb3_pn_stop(usb3);
	spin_unlock_irqrestore(&usb3->lock, flags);

	usb3_disable_pipe_irq(usb3, usb3_ep->num);
	usb3_request_done(usb3_ep, usb3_req, status);

	/* get next usb3_req */
	usb3_req = usb3_get_request(usb3_ep);
	if (usb3_req)
		usb3_start_pipen(usb3_ep, usb3_req);
}

static void usb3_irq_epc_pipen_lsttr(struct renesas_usb3 *usb3, int num)
{
	struct renesas_usb3_ep *usb3_ep = usb3_get_ep(usb3, num);
	struct renesas_usb3_request *usb3_req = usb3_get_request(usb3_ep);

	if (!usb3_req)
		return;

	if (usb3_ep->dir_in) {
		dev_dbg(usb3_to_dev(usb3), "%s: len = %u, actual = %u\n",
			__func__, usb3_req->req.length, usb3_req->req.actual);
		usb3_request_done_pipen(usb3, usb3_ep, usb3_req, 0);
	}
}

static void usb3_irq_epc_pipen_bfrdy(struct renesas_usb3 *usb3, int num)
{
	struct renesas_usb3_ep *usb3_ep = usb3_get_ep(usb3, num);
	struct renesas_usb3_request *usb3_req = usb3_get_request(usb3_ep);
	bool done = false;

	if (!usb3_req)
		return;

	spin_lock(&usb3->lock);
	if (usb3_pn_change(usb3, num))
		goto out;

	if (usb3_ep->dir_in) {
		/* Do not stop the IN pipe here to detect LSTTR interrupt */
		if (!usb3_write_pipe(usb3_ep, usb3_req, USB3_PN_WRITE))
			usb3_clear_bit(usb3, PN_INT_BFRDY, USB3_PN_INT_ENA);
	} else {
		if (!usb3_read_pipe(usb3_ep, usb3_req, USB3_PN_READ))
			done = true;
	}

out:
	/* need to unlock because usb3_request_done_pipen() locks it */
	spin_unlock(&usb3->lock);

	if (done)
		usb3_request_done_pipen(usb3, usb3_ep, usb3_req, 0);
}

static void usb3_irq_epc_pipen(struct renesas_usb3 *usb3, int num)
{
	u32 pn_int_sta;

	spin_lock(&usb3->lock);
	if (usb3_pn_change(usb3, num) < 0) {
		spin_unlock(&usb3->lock);
		return;
	}

	pn_int_sta = usb3_read(usb3, USB3_PN_INT_STA);
	pn_int_sta &= usb3_read(usb3, USB3_PN_INT_ENA);
	usb3_write(usb3, pn_int_sta, USB3_PN_INT_STA);
	spin_unlock(&usb3->lock);
	if (pn_int_sta & PN_INT_LSTTR)
		usb3_irq_epc_pipen_lsttr(usb3, num);
	if (pn_int_sta & PN_INT_BFRDY)
		usb3_irq_epc_pipen_bfrdy(usb3, num);
}

static void usb3_irq_epc_int_2(struct renesas_usb3 *usb3, u32 int_sta_2)
{
	int i;

	for (i = 0; i < usb3->num_usb3_eps; i++) {
		if (int_sta_2 & USB_INT_2_PIPE(i)) {
			if (!i)
				usb3_irq_epc_pipe0(usb3);
			else
				usb3_irq_epc_pipen(usb3, i);
		}
	}
}

static void usb3_irq_idmon_change(struct renesas_usb3 *usb3)
{
	usb3_check_id(usb3);
}

static void usb3_irq_otg_int(struct renesas_usb3 *usb3)
{
	u32 otg_int_sta = usb3_drd_read(usb3, USB3_USB_OTG_INT_STA(usb3));

	otg_int_sta &= usb3_drd_read(usb3, USB3_USB_OTG_INT_ENA(usb3));
	if (otg_int_sta)
		usb3_drd_write(usb3, otg_int_sta, USB3_USB_OTG_INT_STA(usb3));

	if (otg_int_sta & USB_OTG_IDMON(usb3))
		usb3_irq_idmon_change(usb3);
}

static void usb3_irq_epc(struct renesas_usb3 *usb3)
{
	u32 int_sta_1 = usb3_read(usb3, USB3_USB_INT_STA_1);
	u32 int_sta_2 = usb3_read(usb3, USB3_USB_INT_STA_2);

	int_sta_1 &= usb3_read(usb3, USB3_USB_INT_ENA_1);
	if (int_sta_1) {
		usb3_write(usb3, int_sta_1, USB3_USB_INT_STA_1);
		usb3_irq_epc_int_1(usb3, int_sta_1);
	}

	int_sta_2 &= usb3_read(usb3, USB3_USB_INT_ENA_2);
	if (int_sta_2)
		usb3_irq_epc_int_2(usb3, int_sta_2);

	if (!usb3->is_rzv2m)
		usb3_irq_otg_int(usb3);
}

static void usb3_irq_dma_int(struct renesas_usb3 *usb3, u32 dma_sta)
{
	struct renesas_usb3_ep *usb3_ep;
	struct renesas_usb3_request *usb3_req;
	int i, status;

	for (i = 0; i < usb3->num_usb3_eps; i++) {
		if (!(dma_sta & DMA_INT(i)))
			continue;

		usb3_ep = usb3_get_ep(usb3, i);
		if (!(usb3_read(usb3, USB3_AXI_INT_STA) &
		    AXI_INT_PRDEN_CLR_STA(usb3_ep->dma->num)))
			continue;

		usb3_req = usb3_get_request(usb3_ep);
		status = usb3_dma_try_stop(usb3_ep, usb3_req);
		usb3_request_done_pipen(usb3, usb3_ep, usb3_req, status);
	}
}

static void usb3_irq_dma(struct renesas_usb3 *usb3)
{
	u32 dma_sta = usb3_read(usb3, USB3_DMA_INT_STA);

	dma_sta &= usb3_read(usb3, USB3_DMA_INT_ENA);
	if (dma_sta) {
		usb3_write(usb3, dma_sta, USB3_DMA_INT_STA);
		usb3_irq_dma_int(usb3, dma_sta);
	}
}

static irqreturn_t renesas_usb3_irq(int irq, void *_usb3)
{
	struct renesas_usb3 *usb3 = _usb3;
	irqreturn_t ret = IRQ_NONE;
	u32 axi_int_sta = usb3_read(usb3, USB3_AXI_INT_STA);

	if (axi_int_sta & AXI_INT_DMAINT) {
		usb3_irq_dma(usb3);
		ret = IRQ_HANDLED;
	}

	if (axi_int_sta & AXI_INT_EPCINT) {
		usb3_irq_epc(usb3);
		ret = IRQ_HANDLED;
	}

	return ret;
}

static irqreturn_t renesas_usb3_otg_irq(int irq, void *_usb3)
{
	struct renesas_usb3 *usb3 = _usb3;

	usb3_irq_otg_int(usb3);

	return IRQ_HANDLED;
}

static void usb3_write_pn_mod(struct renesas_usb3_ep *usb3_ep,
			      const struct usb_endpoint_descriptor *desc)
{
	struct renesas_usb3 *usb3 = usb3_ep_to_usb3(usb3_ep);
	u32 val = 0;

	val |= usb3_ep->dir_in ? PN_MOD_DIR : 0;
	val |= PN_MOD_TYPE(usb_endpoint_type(desc));
	val |= PN_MOD_EPNUM(usb_endpoint_num(desc));
	usb3_write(usb3, val, USB3_PN_MOD);
}

static u32 usb3_calc_ramarea(int ram_size)
{
	WARN_ON(ram_size > SZ_16K);

	if (ram_size <= SZ_1K)
		return PN_RAMMAP_RAMAREA_1KB;
	else if (ram_size <= SZ_2K)
		return PN_RAMMAP_RAMAREA_2KB;
	else if (ram_size <= SZ_4K)
		return PN_RAMMAP_RAMAREA_4KB;
	else if (ram_size <= SZ_8K)
		return PN_RAMMAP_RAMAREA_8KB;
	else
		return PN_RAMMAP_RAMAREA_16KB;
}

static u32 usb3_calc_rammap_val(struct renesas_usb3_ep *usb3_ep,
				const struct usb_endpoint_descriptor *desc)
{
	int i;
	static const u32 max_packet_array[] = {8, 16, 32, 64, 512};
	u32 mpkt = PN_RAMMAP_MPKT(1024);

	for (i = 0; i < ARRAY_SIZE(max_packet_array); i++) {
		if (usb_endpoint_maxp(desc) <= max_packet_array[i])
			mpkt = PN_RAMMAP_MPKT(max_packet_array[i]);
	}

	return usb3_ep->rammap_val | mpkt;
}

static int usb3_enable_pipe_n(struct renesas_usb3_ep *usb3_ep,
			      const struct usb_endpoint_descriptor *desc)
{
	struct renesas_usb3 *usb3 = usb3_ep_to_usb3(usb3_ep);
	unsigned long flags;

	usb3_ep->dir_in = usb_endpoint_dir_in(desc);

	spin_lock_irqsave(&usb3->lock, flags);
	if (!usb3_pn_change(usb3, usb3_ep->num)) {
		usb3_write_pn_mod(usb3_ep, desc);
		usb3_write(usb3, usb3_calc_rammap_val(usb3_ep, desc),
			   USB3_PN_RAMMAP);
		usb3_pn_con_clear(usb3);
		usb3_set_bit(usb3, PN_CON_EN, USB3_PN_CON);
	}
	spin_unlock_irqrestore(&usb3->lock, flags);

	return 0;
}

static int usb3_disable_pipe_n(struct renesas_usb3_ep *usb3_ep)
{
	struct renesas_usb3 *usb3 = usb3_ep_to_usb3(usb3_ep);
	unsigned long flags;

	usb3_ep->halt = false;

	spin_lock_irqsave(&usb3->lock, flags);
	if (!usb3_pn_change(usb3, usb3_ep->num)) {
		usb3_write(usb3, 0, USB3_PN_INT_ENA);
		usb3_write(usb3, 0, USB3_PN_RAMMAP);
		usb3_clear_bit(usb3, PN_CON_EN, USB3_PN_CON);
	}
	spin_unlock_irqrestore(&usb3->lock, flags);

	return 0;
}

/*------- usb_ep_ops -----------------------------------------------------*/
static int renesas_usb3_ep_enable(struct usb_ep *_ep,
				  const struct usb_endpoint_descriptor *desc)
{
	struct renesas_usb3_ep *usb3_ep = usb_ep_to_usb3_ep(_ep);

	return usb3_enable_pipe_n(usb3_ep, desc);
}

static int renesas_usb3_ep_disable(struct usb_ep *_ep)
{
	struct renesas_usb3_ep *usb3_ep = usb_ep_to_usb3_ep(_ep);
	struct renesas_usb3_request *usb3_req;

	do {
		usb3_req = usb3_get_request(usb3_ep);
		if (!usb3_req)
			break;
		usb3_dma_try_stop(usb3_ep, usb3_req);
		usb3_request_done(usb3_ep, usb3_req, -ESHUTDOWN);
	} while (1);

	return usb3_disable_pipe_n(usb3_ep);
}

static struct usb_request *__renesas_usb3_ep_alloc_request(gfp_t gfp_flags)
{
	struct renesas_usb3_request *usb3_req;

	usb3_req = kzalloc(sizeof(struct renesas_usb3_request), gfp_flags);
	if (!usb3_req)
		return NULL;

	INIT_LIST_HEAD(&usb3_req->queue);

	return &usb3_req->req;
}

static void __renesas_usb3_ep_free_request(struct usb_request *_req)
{
	struct renesas_usb3_request *usb3_req = usb_req_to_usb3_req(_req);

	kfree(usb3_req);
}

static struct usb_request *renesas_usb3_ep_alloc_request(struct usb_ep *_ep,
							 gfp_t gfp_flags)
{
	return __renesas_usb3_ep_alloc_request(gfp_flags);
}

static void renesas_usb3_ep_free_request(struct usb_ep *_ep,
					 struct usb_request *_req)
{
	__renesas_usb3_ep_free_request(_req);
}

static int renesas_usb3_ep_dequeue(struct usb_ep *_ep, struct usb_request *_req)
{
	struct renesas_usb3_ep *usb3_ep = usb_ep_to_usb3_ep(_ep);
	struct renesas_usb3_request *usb3_req = usb_req_to_usb3_req(_req);
	struct renesas_usb3 *usb3 = usb3_ep_to_usb3(usb3_ep);

	dev_dbg(usb3_to_dev(usb3), "ep_dequeue: ep%2d, %u\n", usb3_ep->num,
		_req->length);

	usb3_dma_try_stop(usb3_ep, usb3_req);
	usb3_request_done_pipen(usb3, usb3_ep, usb3_req, -ECONNRESET);

	return 0;
}

static int renesas_usb3_ep_set_halt(struct usb_ep *_ep, int value)
{
	return usb3_set_halt(usb_ep_to_usb3_ep(_ep), !!value, false);
}

static int renesas_usb3_ep_set_wedge(struct usb_ep *_ep)
{
	struct renesas_usb3_ep *usb3_ep = usb_ep_to_usb3_ep(_ep);

	usb3_ep->wedge = true;
	return usb3_set_halt(usb3_ep, true, false);
}

static void renesas_usb3_ep_fifo_flush(struct usb_ep *_ep)
{
	struct renesas_usb3_ep *usb3_ep = usb_ep_to_usb3_ep(_ep);
	struct renesas_usb3 *usb3 = usb3_ep_to_usb3(usb3_ep);
	unsigned long flags;

	if (usb3_ep->num) {
		spin_lock_irqsave(&usb3->lock, flags);
		if (!usb3_pn_change(usb3, usb3_ep->num)) {
			usb3_pn_con_clear(usb3);
			usb3_set_bit(usb3, PN_CON_EN, USB3_PN_CON);
		}
		spin_unlock_irqrestore(&usb3->lock, flags);
	} else {
		usb3_p0_con_clear_buffer(usb3);
	}
}

static const struct usb_ep_ops renesas_usb3_ep_ops = {
	.enable		= renesas_usb3_ep_enable,
	.disable	= renesas_usb3_ep_disable,

	.alloc_request	= renesas_usb3_ep_alloc_request,
	.free_request	= renesas_usb3_ep_free_request,

	.queue		= renesas_usb3_ep_queue,
	.dequeue	= renesas_usb3_ep_dequeue,

	.set_halt	= renesas_usb3_ep_set_halt,
	.set_wedge	= renesas_usb3_ep_set_wedge,
	.fifo_flush	= renesas_usb3_ep_fifo_flush,
};

/*------- usb_gadget_ops -------------------------------------------------*/
static int renesas_usb3_start(struct usb_gadget *gadget,
			      struct usb_gadget_driver *driver)
{
	struct renesas_usb3 *usb3;

	if (!driver || driver->max_speed < USB_SPEED_FULL ||
	    !driver->setup)
		return -EINVAL;

	usb3 = gadget_to_renesas_usb3(gadget);

	if (usb3->is_rzv2m && usb3_is_a_device(usb3))
		return -EBUSY;

	/* hook up the driver */
	usb3->driver = driver;

	if (usb3->phy)
		phy_init(usb3->phy);

	pm_runtime_get_sync(usb3_to_dev(usb3));

	/* Peripheral Reset */
	if (usb3->is_rzv2m)
		rzv2m_usb3drd_reset(usb3_to_dev(usb3)->parent, false);

	renesas_usb3_init_controller(usb3);

	return 0;
}

static int renesas_usb3_stop(struct usb_gadget *gadget)
{
	struct renesas_usb3 *usb3 = gadget_to_renesas_usb3(gadget);

	usb3->softconnect = false;
	usb3->gadget.speed = USB_SPEED_UNKNOWN;
	usb3->driver = NULL;
	if (usb3->is_rzv2m)
		rzv2m_usb3drd_reset(usb3_to_dev(usb3)->parent, false);

	renesas_usb3_stop_controller(usb3);
	phy_exit(usb3->phy);

	pm_runtime_put(usb3_to_dev(usb3));

	return 0;
}

static int renesas_usb3_get_frame(struct usb_gadget *_gadget)
{
	return -EOPNOTSUPP;
}

static int renesas_usb3_pullup(struct usb_gadget *gadget, int is_on)
{
	struct renesas_usb3 *usb3 = gadget_to_renesas_usb3(gadget);

	usb3->softconnect = !!is_on;

	return 0;
}

static int renesas_usb3_set_selfpowered(struct usb_gadget *gadget, int is_self)
{
	gadget->is_selfpowered = !!is_self;

	return 0;
}

static const struct usb_gadget_ops renesas_usb3_gadget_ops = {
	.get_frame		= renesas_usb3_get_frame,
	.udc_start		= renesas_usb3_start,
	.udc_stop		= renesas_usb3_stop,
	.pullup			= renesas_usb3_pullup,
	.set_selfpowered	= renesas_usb3_set_selfpowered,
};

static enum usb_role renesas_usb3_role_switch_get(struct usb_role_switch *sw)
{
	struct renesas_usb3 *usb3 = usb_role_switch_get_drvdata(sw);
	enum usb_role cur_role;

	pm_runtime_get_sync(usb3_to_dev(usb3));
	cur_role = usb3_is_host(usb3) ? USB_ROLE_HOST : USB_ROLE_DEVICE;
	pm_runtime_put(usb3_to_dev(usb3));

	return cur_role;
}

static void handle_ext_role_switch_states(struct device *dev,
					    enum usb_role role)
{
	struct renesas_usb3 *usb3 = dev_get_drvdata(dev);
	struct device *host = usb3->host_dev;
	enum usb_role cur_role = renesas_usb3_role_switch_get(usb3->role_sw);

	switch (role) {
	case USB_ROLE_NONE:
		usb3->connection_state = USB_ROLE_NONE;
		if (!usb3->is_rzv2m && cur_role == USB_ROLE_HOST)
			device_release_driver(host);
		if (usb3->driver) {
			if (usb3->is_rzv2m)
				rzv2m_usb3drd_reset(dev->parent, false);
			usb3_disconnect(usb3);
		}
		usb3_vbus_out(usb3, false);

		if (usb3->is_rzv2m) {
			rzv2m_usb3drd_reset(dev->parent, true);
			device_release_driver(host);
		}
		break;
	case USB_ROLE_DEVICE:
		if (usb3->connection_state == USB_ROLE_NONE) {
			usb3->connection_state = USB_ROLE_DEVICE;
			usb3_set_mode(usb3, false);
			if (usb3->driver) {
				if (usb3->is_rzv2m)
					renesas_usb3_init_controller(usb3);
				usb3_connect(usb3);
			}
		} else if (cur_role == USB_ROLE_HOST)  {
			device_release_driver(host);
			usb3_set_mode(usb3, false);
			if (usb3->driver)
				usb3_connect(usb3);
		}
		usb3_vbus_out(usb3, false);
		break;
	case USB_ROLE_HOST:
		if (usb3->connection_state == USB_ROLE_NONE) {
			if (usb3->driver) {
				if (usb3->is_rzv2m)
					rzv2m_usb3drd_reset(dev->parent, false);
				usb3_disconnect(usb3);
			}

			usb3->connection_state = USB_ROLE_HOST;
			usb3_set_mode(usb3, true);
			usb3_vbus_out(usb3, true);
			if (device_attach(host) < 0)
				dev_err(dev, "device_attach(host) failed\n");
		} else if (cur_role == USB_ROLE_DEVICE) {
			usb3_disconnect(usb3);
			/* Must set the mode before device_attach of the host */
			usb3_set_mode(usb3, true);
			/* This device_attach() might sleep */
			if (device_attach(host) < 0)
				dev_err(dev, "device_attach(host) failed\n");
		}
		break;
	default:
		break;
	}
}

static void handle_role_switch_states(struct device *dev,
					    enum usb_role role)
{
	struct renesas_usb3 *usb3 = dev_get_drvdata(dev);
	struct device *host = usb3->host_dev;
	enum usb_role cur_role = renesas_usb3_role_switch_get(usb3->role_sw);

	if (cur_role == USB_ROLE_HOST && role == USB_ROLE_DEVICE) {
		device_release_driver(host);
		usb3_set_mode(usb3, false);
	} else if (cur_role == USB_ROLE_DEVICE && role == USB_ROLE_HOST) {
		/* Must set the mode before device_attach of the host */
		usb3_set_mode(usb3, true);
		/* This device_attach() might sleep */
		if (device_attach(host) < 0)
			dev_err(dev, "device_attach(host) failed\n");
	}
}

static int renesas_usb3_role_switch_set(struct usb_role_switch *sw,
					enum usb_role role)
{
	struct renesas_usb3 *usb3 = usb_role_switch_get_drvdata(sw);

	pm_runtime_get_sync(usb3_to_dev(usb3));

	if (usb3->role_sw_by_connector)
		handle_ext_role_switch_states(usb3_to_dev(usb3), role);
	else
		handle_role_switch_states(usb3_to_dev(usb3), role);

	pm_runtime_put(usb3_to_dev(usb3));

	return 0;
}

static ssize_t role_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct renesas_usb3 *usb3 = dev_get_drvdata(dev);
	bool new_mode_is_host;

	if (!usb3->driver)
		return -ENODEV;

	if (usb3->forced_b_device)
		return -EBUSY;

	if (sysfs_streq(buf, "host"))
		new_mode_is_host = true;
	else if (sysfs_streq(buf, "peripheral"))
		new_mode_is_host = false;
	else
		return -EINVAL;

	if (new_mode_is_host == usb3_is_host(usb3))
		return -EINVAL;

	usb3_mode_config(usb3, new_mode_is_host, usb3_is_a_device(usb3));

	return count;
}

static ssize_t role_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct renesas_usb3 *usb3 = dev_get_drvdata(dev);

	if (!usb3->driver)
		return -ENODEV;

	return sprintf(buf, "%s\n", usb3_is_host(usb3) ? "host" : "peripheral");
}
static DEVICE_ATTR_RW(role);

static int renesas_usb3_b_device_show(struct seq_file *s, void *unused)
{
	struct renesas_usb3 *usb3 = s->private;

	seq_printf(s, "%d\n", usb3->forced_b_device);

	return 0;
}

static int renesas_usb3_b_device_open(struct inode *inode, struct file *file)
{
	return single_open(file, renesas_usb3_b_device_show, inode->i_private);
}

static ssize_t renesas_usb3_b_device_write(struct file *file,
					   const char __user *ubuf,
					   size_t count, loff_t *ppos)
{
	struct seq_file *s = file->private_data;
	struct renesas_usb3 *usb3 = s->private;
	char buf[32];

	if (!usb3->driver)
		return -ENODEV;

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count)))
		return -EFAULT;

	usb3->start_to_connect = false;
	if (usb3->workaround_for_vbus && usb3->forced_b_device &&
	    !strncmp(buf, "2", 1))
		usb3->start_to_connect = true;
	else if (!strncmp(buf, "1", 1))
		usb3->forced_b_device = true;
	else
		usb3->forced_b_device = false;

	if (usb3->workaround_for_vbus)
		usb3_disconnect(usb3);

	/* Let this driver call usb3_connect() if needed */
	usb3_check_id(usb3);

	return count;
}

static const struct file_operations renesas_usb3_b_device_fops = {
	.open = renesas_usb3_b_device_open,
	.write = renesas_usb3_b_device_write,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static void renesas_usb3_debugfs_init(struct renesas_usb3 *usb3,
				      struct device *dev)
{
	usb3->dentry = debugfs_create_dir(dev_name(dev), usb_debug_root);

	debugfs_create_file("b_device", 0644, usb3->dentry, usb3,
			    &renesas_usb3_b_device_fops);
}

/*------- platform_driver ------------------------------------------------*/
static void renesas_usb3_remove(struct platform_device *pdev)
{
	struct renesas_usb3 *usb3 = platform_get_drvdata(pdev);

	debugfs_remove_recursive(usb3->dentry);
	put_device(usb3->host_dev);
	device_remove_file(&pdev->dev, &dev_attr_role);

	cancel_work_sync(&usb3->role_work);
	usb_role_switch_unregister(usb3->role_sw);

	usb_del_gadget_udc(&usb3->gadget);
	reset_control_assert(usb3->usbp_rstc);
	renesas_usb3_dma_free_prd(usb3, &pdev->dev);

	__renesas_usb3_ep_free_request(usb3->ep0_req);
	pm_runtime_disable(&pdev->dev);
}

static int renesas_usb3_init_ep(struct renesas_usb3 *usb3, struct device *dev,
				const struct renesas_usb3_priv *priv)
{
	struct renesas_usb3_ep *usb3_ep;
	int i;

	/* calculate num_usb3_eps from renesas_usb3_priv */
	usb3->num_usb3_eps = priv->ramsize_per_ramif * priv->num_ramif * 2 /
			     priv->ramsize_per_pipe + 1;

	if (usb3->num_usb3_eps > USB3_MAX_NUM_PIPES(usb3))
		usb3->num_usb3_eps = USB3_MAX_NUM_PIPES(usb3);

	usb3->usb3_ep = devm_kcalloc(dev,
				     usb3->num_usb3_eps, sizeof(*usb3_ep),
				     GFP_KERNEL);
	if (!usb3->usb3_ep)
		return -ENOMEM;

	dev_dbg(dev, "%s: num_usb3_eps = %d\n", __func__, usb3->num_usb3_eps);
	/*
	 * This driver prepares pipes as follows:
	 *  - odd pipes = IN pipe
	 *  - even pipes = OUT pipe (except pipe 0)
	 */
	usb3_for_each_ep(usb3_ep, usb3, i) {
		snprintf(usb3_ep->ep_name, sizeof(usb3_ep->ep_name), "ep%d", i);
		usb3_ep->usb3 = usb3;
		usb3_ep->num = i;
		usb3_ep->ep.name = usb3_ep->ep_name;
		usb3_ep->ep.ops = &renesas_usb3_ep_ops;
		INIT_LIST_HEAD(&usb3_ep->queue);
		INIT_LIST_HEAD(&usb3_ep->ep.ep_list);
		if (!i) {
			/* for control pipe */
			usb3->gadget.ep0 = &usb3_ep->ep;
			usb_ep_set_maxpacket_limit(&usb3_ep->ep,
						USB3_EP0_SS_MAX_PACKET_SIZE);
			usb3_ep->ep.caps.type_control = true;
			usb3_ep->ep.caps.dir_in = true;
			usb3_ep->ep.caps.dir_out = true;
			continue;
		}

		/* for bulk or interrupt pipe */
		usb_ep_set_maxpacket_limit(&usb3_ep->ep, ~0);
		list_add_tail(&usb3_ep->ep.ep_list, &usb3->gadget.ep_list);
		usb3_ep->ep.caps.type_bulk = true;
		usb3_ep->ep.caps.type_int = true;
		if (i & 1)
			usb3_ep->ep.caps.dir_in = true;
		else
			usb3_ep->ep.caps.dir_out = true;
	}

	return 0;
}

static void renesas_usb3_init_ram(struct renesas_usb3 *usb3, struct device *dev,
				  const struct renesas_usb3_priv *priv)
{
	struct renesas_usb3_ep *usb3_ep;
	int i;
	u32 ramif[2], basead[2];	/* index 0 = for IN pipes */
	u32 *cur_ramif, *cur_basead;
	u32 val;

	memset(ramif, 0, sizeof(ramif));
	memset(basead, 0, sizeof(basead));

	/*
	 * This driver prepares pipes as follows:
	 *  - all pipes = the same size as "ramsize_per_pipe"
	 * Please refer to the "Method of Specifying RAM Mapping"
	 */
	usb3_for_each_ep(usb3_ep, usb3, i) {
		if (!i)
			continue;	/* out of scope if ep num = 0 */
		if (usb3_ep->ep.caps.dir_in) {
			cur_ramif = &ramif[0];
			cur_basead = &basead[0];
		} else {
			cur_ramif = &ramif[1];
			cur_basead = &basead[1];
		}

		if (*cur_basead > priv->ramsize_per_ramif)
			continue;	/* out of memory for IN or OUT pipe */

		/* calculate rammap_val */
		val = PN_RAMMAP_RAMIF(*cur_ramif);
		val |= usb3_calc_ramarea(priv->ramsize_per_pipe);
		val |= PN_RAMMAP_BASEAD(*cur_basead);
		usb3_ep->rammap_val = val;

		dev_dbg(dev, "ep%2d: val = %08x, ramif = %d, base = %x\n",
			i, val, *cur_ramif, *cur_basead);

		/* update current ramif */
		if (*cur_ramif + 1 == priv->num_ramif) {
			*cur_ramif = 0;
			*cur_basead += priv->ramsize_per_pipe;
		} else {
			(*cur_ramif)++;
		}
	}
}

static const struct renesas_usb3_priv renesas_usb3_priv_gen3 = {
	.ramsize_per_ramif = SZ_16K,
	.num_ramif = 4,
	.ramsize_per_pipe = SZ_4K,
};

static const struct renesas_usb3_priv renesas_usb3_priv_r8a77990 = {
	.ramsize_per_ramif = SZ_16K,
	.num_ramif = 4,
	.ramsize_per_pipe = SZ_4K,
	.workaround_for_vbus = true,
};

static const struct renesas_usb3_priv renesas_usb3_priv_rzv2m = {
	.ramsize_per_ramif = SZ_16K,
	.num_ramif = 1,
	.ramsize_per_pipe = SZ_4K,
	.is_rzv2m = true,
};

static const struct of_device_id usb3_of_match[] = {
	{
		.compatible = "renesas,r8a774c0-usb3-peri",
		.data = &renesas_usb3_priv_r8a77990,
	}, {
		.compatible = "renesas,r8a7795-usb3-peri",
		.data = &renesas_usb3_priv_gen3,
	}, {
		.compatible = "renesas,r8a77990-usb3-peri",
		.data = &renesas_usb3_priv_r8a77990,
	}, {
		.compatible = "renesas,rzv2m-usb3-peri",
		.data = &renesas_usb3_priv_rzv2m,
	}, {
		.compatible = "renesas,rcar-gen3-usb3-peri",
		.data = &renesas_usb3_priv_gen3,
	},
	{ },
};
MODULE_DEVICE_TABLE(of, usb3_of_match);

static const unsigned int renesas_usb3_cable[] = {
	EXTCON_USB,
	EXTCON_USB_HOST,
	EXTCON_NONE,
};

static struct usb_role_switch_desc renesas_usb3_role_switch_desc = {
	.set = renesas_usb3_role_switch_set,
	.get = renesas_usb3_role_switch_get,
	.allow_userspace_control = true,
};

static int renesas_usb3_probe(struct platform_device *pdev)
{
	struct renesas_usb3 *usb3;
	int irq, ret;
	const struct renesas_usb3_priv *priv;

	priv = of_device_get_match_data(&pdev->dev);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	usb3 = devm_kzalloc(&pdev->dev, sizeof(*usb3), GFP_KERNEL);
	if (!usb3)
		return -ENOMEM;

	usb3->is_rzv2m = priv->is_rzv2m;

	usb3->reg = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(usb3->reg))
		return PTR_ERR(usb3->reg);

	platform_set_drvdata(pdev, usb3);
	spin_lock_init(&usb3->lock);

	usb3->gadget.ops = &renesas_usb3_gadget_ops;
	usb3->gadget.name = udc_name;
	usb3->gadget.max_speed = USB_SPEED_SUPER;
	INIT_LIST_HEAD(&usb3->gadget.ep_list);
	ret = renesas_usb3_init_ep(usb3, &pdev->dev, priv);
	if (ret < 0)
		return ret;
	renesas_usb3_init_ram(usb3, &pdev->dev, priv);

	ret = devm_request_irq(&pdev->dev, irq, renesas_usb3_irq, 0,
			       dev_name(&pdev->dev), usb3);
	if (ret < 0)
		return ret;

	if (usb3->is_rzv2m) {
		struct rzv2m_usb3drd *ddata = dev_get_drvdata(pdev->dev.parent);

		usb3->drd_reg = ddata->reg;
		ret = devm_request_irq(&pdev->dev, ddata->drd_irq,
				       renesas_usb3_otg_irq, 0,
				       dev_name(&pdev->dev), usb3);
		if (ret < 0)
			return ret;
	}

	INIT_WORK(&usb3->extcon_work, renesas_usb3_extcon_work);
	usb3->extcon = devm_extcon_dev_allocate(&pdev->dev, renesas_usb3_cable);
	if (IS_ERR(usb3->extcon))
		return PTR_ERR(usb3->extcon);

	ret = devm_extcon_dev_register(&pdev->dev, usb3->extcon);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to register extcon\n");
		return ret;
	}

	/* for ep0 handling */
	usb3->ep0_req = __renesas_usb3_ep_alloc_request(GFP_KERNEL);
	if (!usb3->ep0_req)
		return -ENOMEM;

	ret = renesas_usb3_dma_alloc_prd(usb3, &pdev->dev);
	if (ret < 0)
		goto err_alloc_prd;

	/*
	 * This is optional. So, if this driver cannot get a phy,
	 * this driver will not handle a phy anymore.
	 */
	usb3->phy = devm_phy_optional_get(&pdev->dev, "usb");
	if (IS_ERR(usb3->phy)) {
		ret = PTR_ERR(usb3->phy);
		goto err_add_udc;
	}

	usb3->usbp_rstc = devm_reset_control_get_optional_shared(&pdev->dev,
								 NULL);
	if (IS_ERR(usb3->usbp_rstc)) {
		ret = PTR_ERR(usb3->usbp_rstc);
		goto err_add_udc;
	}

	reset_control_deassert(usb3->usbp_rstc);

	pm_runtime_enable(&pdev->dev);
	ret = usb_add_gadget_udc(&pdev->dev, &usb3->gadget);
	if (ret < 0)
		goto err_reset;

	ret = device_create_file(&pdev->dev, &dev_attr_role);
	if (ret < 0)
		goto err_dev_create;

	if (device_property_read_bool(&pdev->dev, "usb-role-switch")) {
		usb3->role_sw_by_connector = true;
		renesas_usb3_role_switch_desc.fwnode = dev_fwnode(&pdev->dev);
	}

	renesas_usb3_role_switch_desc.driver_data = usb3;

	INIT_WORK(&usb3->role_work, renesas_usb3_role_work);
	usb3->role_sw = usb_role_switch_register(&pdev->dev,
					&renesas_usb3_role_switch_desc);
	if (!IS_ERR(usb3->role_sw)) {
		usb3->host_dev = usb_of_get_companion_dev(&pdev->dev);
		if (!usb3->host_dev) {
			/* If not found, this driver will not use a role sw */
			usb_role_switch_unregister(usb3->role_sw);
			usb3->role_sw = NULL;
		}
	} else {
		usb3->role_sw = NULL;
	}

	usb3->workaround_for_vbus = priv->workaround_for_vbus;

	renesas_usb3_debugfs_init(usb3, &pdev->dev);

	dev_info(&pdev->dev, "probed%s\n", usb3->phy ? " with phy" : "");

	return 0;

err_dev_create:
	usb_del_gadget_udc(&usb3->gadget);

err_reset:
	reset_control_assert(usb3->usbp_rstc);

err_add_udc:
	renesas_usb3_dma_free_prd(usb3, &pdev->dev);

err_alloc_prd:
	__renesas_usb3_ep_free_request(usb3->ep0_req);

	return ret;
}

static int renesas_usb3_suspend(struct device *dev)
{
	struct renesas_usb3 *usb3 = dev_get_drvdata(dev);

	/* Not started */
	if (!usb3->driver)
		return 0;

	renesas_usb3_stop_controller(usb3);
	phy_exit(usb3->phy);
	pm_runtime_put(dev);

	return 0;
}

static int renesas_usb3_resume(struct device *dev)
{
	struct renesas_usb3 *usb3 = dev_get_drvdata(dev);

	/* Not started */
	if (!usb3->driver)
		return 0;

	if (usb3->phy)
		phy_init(usb3->phy);
	pm_runtime_get_sync(dev);
	renesas_usb3_init_controller(usb3);

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(renesas_usb3_pm_ops, renesas_usb3_suspend,
				renesas_usb3_resume);

static struct platform_driver renesas_usb3_driver = {
	.probe		= renesas_usb3_probe,
	.remove		= renesas_usb3_remove,
	.driver		= {
		.name =	udc_name,
		.pm		= pm_sleep_ptr(&renesas_usb3_pm_ops),
		.of_match_table = usb3_of_match,
	},
};
module_platform_driver(renesas_usb3_driver);

MODULE_DESCRIPTION("Renesas USB3.0 Peripheral driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Yoshihiro Shimoda <yoshihiro.shimoda.uh@renesas.com>");
