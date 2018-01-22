// SPDX-License-Identifier: GPL-2.0
/*
 * mtu3_core.c - hardware access layer and gadget init/exit of
 *                     MediaTek usb3 Dual-Role Controller Driver
 *
 * Copyright (C) 2016 MediaTek Inc.
 *
 * Author: Chunfeng Yun <chunfeng.yun@mediatek.com>
 */

#include <linux/dma-mapping.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>

#include "mtu3.h"

static int ep_fifo_alloc(struct mtu3_ep *mep, u32 seg_size)
{
	struct mtu3_fifo_info *fifo = mep->fifo;
	u32 num_bits = DIV_ROUND_UP(seg_size, MTU3_EP_FIFO_UNIT);
	u32 start_bit;

	/* ensure that @mep->fifo_seg_size is power of two */
	num_bits = roundup_pow_of_two(num_bits);
	if (num_bits > fifo->limit)
		return -EINVAL;

	mep->fifo_seg_size = num_bits * MTU3_EP_FIFO_UNIT;
	num_bits = num_bits * (mep->slot + 1);
	start_bit = bitmap_find_next_zero_area(fifo->bitmap,
			fifo->limit, 0, num_bits, 0);
	if (start_bit >= fifo->limit)
		return -EOVERFLOW;

	bitmap_set(fifo->bitmap, start_bit, num_bits);
	mep->fifo_size = num_bits * MTU3_EP_FIFO_UNIT;
	mep->fifo_addr = fifo->base + MTU3_EP_FIFO_UNIT * start_bit;

	dev_dbg(mep->mtu->dev, "%s fifo:%#x/%#x, start_bit: %d\n",
		__func__, mep->fifo_seg_size, mep->fifo_size, start_bit);

	return mep->fifo_addr;
}

static void ep_fifo_free(struct mtu3_ep *mep)
{
	struct mtu3_fifo_info *fifo = mep->fifo;
	u32 addr = mep->fifo_addr;
	u32 bits = mep->fifo_size / MTU3_EP_FIFO_UNIT;
	u32 start_bit;

	if (unlikely(addr < fifo->base || bits > fifo->limit))
		return;

	start_bit = (addr - fifo->base) / MTU3_EP_FIFO_UNIT;
	bitmap_clear(fifo->bitmap, start_bit, bits);
	mep->fifo_size = 0;
	mep->fifo_seg_size = 0;

	dev_dbg(mep->mtu->dev, "%s size:%#x/%#x, start_bit: %d\n",
		__func__, mep->fifo_seg_size, mep->fifo_size, start_bit);
}

/* enable/disable U3D SS function */
static inline void mtu3_ss_func_set(struct mtu3 *mtu, bool enable)
{
	/* If usb3_en==0, LTSSM will go to SS.Disable state */
	if (enable)
		mtu3_setbits(mtu->mac_base, U3D_USB3_CONFIG, USB3_EN);
	else
		mtu3_clrbits(mtu->mac_base, U3D_USB3_CONFIG, USB3_EN);

	dev_dbg(mtu->dev, "USB3_EN = %d\n", !!enable);
}

/* set/clear U3D HS device soft connect */
static inline void mtu3_hs_softconn_set(struct mtu3 *mtu, bool enable)
{
	if (enable) {
		mtu3_setbits(mtu->mac_base, U3D_POWER_MANAGEMENT,
			SOFT_CONN | SUSPENDM_ENABLE);
	} else {
		mtu3_clrbits(mtu->mac_base, U3D_POWER_MANAGEMENT,
			SOFT_CONN | SUSPENDM_ENABLE);
	}
	dev_dbg(mtu->dev, "SOFTCONN = %d\n", !!enable);
}

/* only port0 of U2/U3 supports device mode */
static int mtu3_device_enable(struct mtu3 *mtu)
{
	void __iomem *ibase = mtu->ippc_base;
	u32 check_clk = 0;

	mtu3_clrbits(ibase, U3D_SSUSB_IP_PW_CTRL2, SSUSB_IP_DEV_PDN);

	if (mtu->is_u3_ip) {
		check_clk = SSUSB_U3_MAC_RST_B_STS;
		mtu3_clrbits(ibase, SSUSB_U3_CTRL(0),
			(SSUSB_U3_PORT_DIS | SSUSB_U3_PORT_PDN |
			SSUSB_U3_PORT_HOST_SEL));
	}
	mtu3_clrbits(ibase, SSUSB_U2_CTRL(0),
		(SSUSB_U2_PORT_DIS | SSUSB_U2_PORT_PDN |
		SSUSB_U2_PORT_HOST_SEL));

	if (mtu->ssusb->dr_mode == USB_DR_MODE_OTG)
		mtu3_setbits(ibase, SSUSB_U2_CTRL(0), SSUSB_U2_PORT_OTG_SEL);

	return ssusb_check_clocks(mtu->ssusb, check_clk);
}

static void mtu3_device_disable(struct mtu3 *mtu)
{
	void __iomem *ibase = mtu->ippc_base;

	if (mtu->is_u3_ip)
		mtu3_setbits(ibase, SSUSB_U3_CTRL(0),
			(SSUSB_U3_PORT_DIS | SSUSB_U3_PORT_PDN));

	mtu3_setbits(ibase, SSUSB_U2_CTRL(0),
		SSUSB_U2_PORT_DIS | SSUSB_U2_PORT_PDN);

	if (mtu->ssusb->dr_mode == USB_DR_MODE_OTG)
		mtu3_clrbits(ibase, SSUSB_U2_CTRL(0), SSUSB_U2_PORT_OTG_SEL);

	mtu3_setbits(ibase, U3D_SSUSB_IP_PW_CTRL2, SSUSB_IP_DEV_PDN);
}

/* reset U3D's device module. */
static void mtu3_device_reset(struct mtu3 *mtu)
{
	void __iomem *ibase = mtu->ippc_base;

	mtu3_setbits(ibase, U3D_SSUSB_DEV_RST_CTRL, SSUSB_DEV_SW_RST);
	udelay(1);
	mtu3_clrbits(ibase, U3D_SSUSB_DEV_RST_CTRL, SSUSB_DEV_SW_RST);
}

/* disable all interrupts */
static void mtu3_intr_disable(struct mtu3 *mtu)
{
	void __iomem *mbase = mtu->mac_base;

	/* Disable level 1 interrupts */
	mtu3_writel(mbase, U3D_LV1IECR, ~0x0);
	/* Disable endpoint interrupts */
	mtu3_writel(mbase, U3D_EPIECR, ~0x0);
}

static void mtu3_intr_status_clear(struct mtu3 *mtu)
{
	void __iomem *mbase = mtu->mac_base;

	/* Clear EP0 and Tx/Rx EPn interrupts status */
	mtu3_writel(mbase, U3D_EPISR, ~0x0);
	/* Clear U2 USB common interrupts status */
	mtu3_writel(mbase, U3D_COMMON_USB_INTR, ~0x0);
	/* Clear U3 LTSSM interrupts status */
	mtu3_writel(mbase, U3D_LTSSM_INTR, ~0x0);
	/* Clear speed change interrupt status */
	mtu3_writel(mbase, U3D_DEV_LINK_INTR, ~0x0);
}

/* enable system global interrupt */
static void mtu3_intr_enable(struct mtu3 *mtu)
{
	void __iomem *mbase = mtu->mac_base;
	u32 value;

	/*Enable level 1 interrupts (BMU, QMU, MAC3, DMA, MAC2, EPCTL) */
	value = BMU_INTR | QMU_INTR | MAC3_INTR | MAC2_INTR | EP_CTRL_INTR;
	mtu3_writel(mbase, U3D_LV1IESR, value);

	/* Enable U2 common USB interrupts */
	value = SUSPEND_INTR | RESUME_INTR | RESET_INTR;
	mtu3_writel(mbase, U3D_COMMON_USB_INTR_ENABLE, value);

	if (mtu->is_u3_ip) {
		/* Enable U3 LTSSM interrupts */
		value = HOT_RST_INTR | WARM_RST_INTR | VBUS_RISE_INTR |
		    VBUS_FALL_INTR | ENTER_U3_INTR | EXIT_U3_INTR;
		mtu3_writel(mbase, U3D_LTSSM_INTR_ENABLE, value);
	}

	/* Enable QMU interrupts. */
	value = TXQ_CSERR_INT | TXQ_LENERR_INT | RXQ_CSERR_INT |
			RXQ_LENERR_INT | RXQ_ZLPERR_INT;
	mtu3_writel(mbase, U3D_QIESR1, value);

	/* Enable speed change interrupt */
	mtu3_writel(mbase, U3D_DEV_LINK_INTR_ENABLE, SSUSB_DEV_SPEED_CHG_INTR);
}

/* set/clear the stall and toggle bits for non-ep0 */
void mtu3_ep_stall_set(struct mtu3_ep *mep, bool set)
{
	struct mtu3 *mtu = mep->mtu;
	void __iomem *mbase = mtu->mac_base;
	u8 epnum = mep->epnum;
	u32 csr;

	if (mep->is_in) {	/* TX */
		csr = mtu3_readl(mbase, MU3D_EP_TXCR0(epnum)) & TX_W1C_BITS;
		if (set)
			csr |= TX_SENDSTALL;
		else
			csr = (csr & (~TX_SENDSTALL)) | TX_SENTSTALL;
		mtu3_writel(mbase, MU3D_EP_TXCR0(epnum), csr);
	} else {	/* RX */
		csr = mtu3_readl(mbase, MU3D_EP_RXCR0(epnum)) & RX_W1C_BITS;
		if (set)
			csr |= RX_SENDSTALL;
		else
			csr = (csr & (~RX_SENDSTALL)) | RX_SENTSTALL;
		mtu3_writel(mbase, MU3D_EP_RXCR0(epnum), csr);
	}

	if (!set) {
		mtu3_setbits(mbase, U3D_EP_RST, EP_RST(mep->is_in, epnum));
		mtu3_clrbits(mbase, U3D_EP_RST, EP_RST(mep->is_in, epnum));
		mep->flags &= ~MTU3_EP_STALL;
	} else {
		mep->flags |= MTU3_EP_STALL;
	}

	dev_dbg(mtu->dev, "%s: %s\n", mep->name,
		set ? "SEND STALL" : "CLEAR STALL, with EP RESET");
}

void mtu3_dev_on_off(struct mtu3 *mtu, int is_on)
{
	if (mtu->is_u3_ip && mtu->max_speed >= USB_SPEED_SUPER)
		mtu3_ss_func_set(mtu, is_on);
	else
		mtu3_hs_softconn_set(mtu, is_on);

	dev_info(mtu->dev, "gadget (%s) pullup D%s\n",
		usb_speed_string(mtu->max_speed), is_on ? "+" : "-");
}

void mtu3_start(struct mtu3 *mtu)
{
	void __iomem *mbase = mtu->mac_base;

	dev_dbg(mtu->dev, "%s devctl 0x%x\n", __func__,
		mtu3_readl(mbase, U3D_DEVICE_CONTROL));

	mtu3_clrbits(mtu->ippc_base, U3D_SSUSB_IP_PW_CTRL2, SSUSB_IP_DEV_PDN);

	/*
	 * When disable U2 port, USB2_CSR's register will be reset to
	 * default value after re-enable it again(HS is enabled by default).
	 * So if force mac to work as FS, disable HS function.
	 */
	if (mtu->max_speed == USB_SPEED_FULL)
		mtu3_clrbits(mbase, U3D_POWER_MANAGEMENT, HS_ENABLE);

	/* Initialize the default interrupts */
	mtu3_intr_enable(mtu);
	mtu->is_active = 1;

	if (mtu->softconnect)
		mtu3_dev_on_off(mtu, 1);
}

void mtu3_stop(struct mtu3 *mtu)
{
	dev_dbg(mtu->dev, "%s\n", __func__);

	mtu3_intr_disable(mtu);
	mtu3_intr_status_clear(mtu);

	if (mtu->softconnect)
		mtu3_dev_on_off(mtu, 0);

	mtu->is_active = 0;
	mtu3_setbits(mtu->ippc_base, U3D_SSUSB_IP_PW_CTRL2, SSUSB_IP_DEV_PDN);
}

/* for non-ep0 */
int mtu3_config_ep(struct mtu3 *mtu, struct mtu3_ep *mep,
			int interval, int burst, int mult)
{
	void __iomem *mbase = mtu->mac_base;
	int epnum = mep->epnum;
	u32 csr0, csr1, csr2;
	int fifo_sgsz, fifo_addr;
	int num_pkts;

	fifo_addr = ep_fifo_alloc(mep, mep->maxp);
	if (fifo_addr < 0) {
		dev_err(mtu->dev, "alloc ep fifo failed(%d)\n", mep->maxp);
		return -ENOMEM;
	}
	fifo_sgsz = ilog2(mep->fifo_seg_size);
	dev_dbg(mtu->dev, "%s fifosz: %x(%x/%x)\n", __func__, fifo_sgsz,
		mep->fifo_seg_size, mep->fifo_size);

	if (mep->is_in) {
		csr0 = TX_TXMAXPKTSZ(mep->maxp);
		csr0 |= TX_DMAREQEN;

		num_pkts = (burst + 1) * (mult + 1) - 1;
		csr1 = TX_SS_BURST(burst) | TX_SLOT(mep->slot);
		csr1 |= TX_MAX_PKT(num_pkts) | TX_MULT(mult);

		csr2 = TX_FIFOADDR(fifo_addr >> 4);
		csr2 |= TX_FIFOSEGSIZE(fifo_sgsz);

		switch (mep->type) {
		case USB_ENDPOINT_XFER_BULK:
			csr1 |= TX_TYPE(TYPE_BULK);
			break;
		case USB_ENDPOINT_XFER_ISOC:
			csr1 |= TX_TYPE(TYPE_ISO);
			csr2 |= TX_BINTERVAL(interval);
			break;
		case USB_ENDPOINT_XFER_INT:
			csr1 |= TX_TYPE(TYPE_INT);
			csr2 |= TX_BINTERVAL(interval);
			break;
		}

		/* Enable QMU Done interrupt */
		mtu3_setbits(mbase, U3D_QIESR0, QMU_TX_DONE_INT(epnum));

		mtu3_writel(mbase, MU3D_EP_TXCR0(epnum), csr0);
		mtu3_writel(mbase, MU3D_EP_TXCR1(epnum), csr1);
		mtu3_writel(mbase, MU3D_EP_TXCR2(epnum), csr2);

		dev_dbg(mtu->dev, "U3D_TX%d CSR0:%#x, CSR1:%#x, CSR2:%#x\n",
			epnum, mtu3_readl(mbase, MU3D_EP_TXCR0(epnum)),
			mtu3_readl(mbase, MU3D_EP_TXCR1(epnum)),
			mtu3_readl(mbase, MU3D_EP_TXCR2(epnum)));
	} else {
		csr0 = RX_RXMAXPKTSZ(mep->maxp);
		csr0 |= RX_DMAREQEN;

		num_pkts = (burst + 1) * (mult + 1) - 1;
		csr1 = RX_SS_BURST(burst) | RX_SLOT(mep->slot);
		csr1 |= RX_MAX_PKT(num_pkts) | RX_MULT(mult);

		csr2 = RX_FIFOADDR(fifo_addr >> 4);
		csr2 |= RX_FIFOSEGSIZE(fifo_sgsz);

		switch (mep->type) {
		case USB_ENDPOINT_XFER_BULK:
			csr1 |= RX_TYPE(TYPE_BULK);
			break;
		case USB_ENDPOINT_XFER_ISOC:
			csr1 |= RX_TYPE(TYPE_ISO);
			csr2 |= RX_BINTERVAL(interval);
			break;
		case USB_ENDPOINT_XFER_INT:
			csr1 |= RX_TYPE(TYPE_INT);
			csr2 |= RX_BINTERVAL(interval);
			break;
		}

		/*Enable QMU Done interrupt */
		mtu3_setbits(mbase, U3D_QIESR0, QMU_RX_DONE_INT(epnum));

		mtu3_writel(mbase, MU3D_EP_RXCR0(epnum), csr0);
		mtu3_writel(mbase, MU3D_EP_RXCR1(epnum), csr1);
		mtu3_writel(mbase, MU3D_EP_RXCR2(epnum), csr2);

		dev_dbg(mtu->dev, "U3D_RX%d CSR0:%#x, CSR1:%#x, CSR2:%#x\n",
			epnum, mtu3_readl(mbase, MU3D_EP_RXCR0(epnum)),
			mtu3_readl(mbase, MU3D_EP_RXCR1(epnum)),
			mtu3_readl(mbase, MU3D_EP_RXCR2(epnum)));
	}

	dev_dbg(mtu->dev, "csr0:%#x, csr1:%#x, csr2:%#x\n", csr0, csr1, csr2);
	dev_dbg(mtu->dev, "%s: %s, fifo-addr:%#x, fifo-size:%#x(%#x/%#x)\n",
		__func__, mep->name, mep->fifo_addr, mep->fifo_size,
		fifo_sgsz, mep->fifo_seg_size);

	return 0;
}

/* for non-ep0 */
void mtu3_deconfig_ep(struct mtu3 *mtu, struct mtu3_ep *mep)
{
	void __iomem *mbase = mtu->mac_base;
	int epnum = mep->epnum;

	if (mep->is_in) {
		mtu3_writel(mbase, MU3D_EP_TXCR0(epnum), 0);
		mtu3_writel(mbase, MU3D_EP_TXCR1(epnum), 0);
		mtu3_writel(mbase, MU3D_EP_TXCR2(epnum), 0);
		mtu3_setbits(mbase, U3D_QIECR0, QMU_TX_DONE_INT(epnum));
	} else {
		mtu3_writel(mbase, MU3D_EP_RXCR0(epnum), 0);
		mtu3_writel(mbase, MU3D_EP_RXCR1(epnum), 0);
		mtu3_writel(mbase, MU3D_EP_RXCR2(epnum), 0);
		mtu3_setbits(mbase, U3D_QIECR0, QMU_RX_DONE_INT(epnum));
	}

	ep_fifo_free(mep);

	dev_dbg(mtu->dev, "%s: %s\n", __func__, mep->name);
}

/*
 * Two scenarios:
 * 1. when device IP supports SS, the fifo of EP0, TX EPs, RX EPs
 *	are separated;
 * 2. when supports only HS, the fifo is shared for all EPs, and
 *	the capability registers of @EPNTXFFSZ or @EPNRXFFSZ indicate
 *	the total fifo size of non-ep0, and ep0's is fixed to 64B,
 *	so the total fifo size is 64B + @EPNTXFFSZ;
 *	Due to the first 64B should be reserved for EP0, non-ep0's fifo
 *	starts from offset 64 and are divided into two equal parts for
 *	TX or RX EPs for simplification.
 */
static void get_ep_fifo_config(struct mtu3 *mtu)
{
	struct mtu3_fifo_info *tx_fifo;
	struct mtu3_fifo_info *rx_fifo;
	u32 fifosize;

	if (mtu->is_u3_ip) {
		fifosize = mtu3_readl(mtu->mac_base, U3D_CAP_EPNTXFFSZ);
		tx_fifo = &mtu->tx_fifo;
		tx_fifo->base = 0;
		tx_fifo->limit = fifosize / MTU3_EP_FIFO_UNIT;
		bitmap_zero(tx_fifo->bitmap, MTU3_FIFO_BIT_SIZE);

		fifosize = mtu3_readl(mtu->mac_base, U3D_CAP_EPNRXFFSZ);
		rx_fifo = &mtu->rx_fifo;
		rx_fifo->base = 0;
		rx_fifo->limit = fifosize / MTU3_EP_FIFO_UNIT;
		bitmap_zero(rx_fifo->bitmap, MTU3_FIFO_BIT_SIZE);
		mtu->slot = MTU3_U3_IP_SLOT_DEFAULT;
	} else {
		fifosize = mtu3_readl(mtu->mac_base, U3D_CAP_EPNTXFFSZ);
		tx_fifo = &mtu->tx_fifo;
		tx_fifo->base = MTU3_U2_IP_EP0_FIFO_SIZE;
		tx_fifo->limit = (fifosize / MTU3_EP_FIFO_UNIT) >> 1;
		bitmap_zero(tx_fifo->bitmap, MTU3_FIFO_BIT_SIZE);

		rx_fifo = &mtu->rx_fifo;
		rx_fifo->base =
			tx_fifo->base + tx_fifo->limit * MTU3_EP_FIFO_UNIT;
		rx_fifo->limit = tx_fifo->limit;
		bitmap_zero(rx_fifo->bitmap, MTU3_FIFO_BIT_SIZE);
		mtu->slot = MTU3_U2_IP_SLOT_DEFAULT;
	}

	dev_dbg(mtu->dev, "%s, TX: base-%d, limit-%d; RX: base-%d, limit-%d\n",
		__func__, tx_fifo->base, tx_fifo->limit,
		rx_fifo->base, rx_fifo->limit);
}

void mtu3_ep0_setup(struct mtu3 *mtu)
{
	u32 maxpacket = mtu->g.ep0->maxpacket;
	u32 csr;

	dev_dbg(mtu->dev, "%s maxpacket: %d\n", __func__, maxpacket);

	csr = mtu3_readl(mtu->mac_base, U3D_EP0CSR);
	csr &= ~EP0_MAXPKTSZ_MSK;
	csr |= EP0_MAXPKTSZ(maxpacket);
	csr &= EP0_W1C_BITS;
	mtu3_writel(mtu->mac_base, U3D_EP0CSR, csr);

	/* Enable EP0 interrupt */
	mtu3_writel(mtu->mac_base, U3D_EPIESR, EP0ISR);
}

static int mtu3_mem_alloc(struct mtu3 *mtu)
{
	void __iomem *mbase = mtu->mac_base;
	struct mtu3_ep *ep_array;
	int in_ep_num, out_ep_num;
	u32 cap_epinfo;
	int ret;
	int i;

	cap_epinfo = mtu3_readl(mbase, U3D_CAP_EPINFO);
	in_ep_num = CAP_TX_EP_NUM(cap_epinfo);
	out_ep_num = CAP_RX_EP_NUM(cap_epinfo);

	dev_info(mtu->dev, "fifosz/epnum: Tx=%#x/%d, Rx=%#x/%d\n",
		 mtu3_readl(mbase, U3D_CAP_EPNTXFFSZ), in_ep_num,
		 mtu3_readl(mbase, U3D_CAP_EPNRXFFSZ), out_ep_num);

	/* one for ep0, another is reserved */
	mtu->num_eps = min(in_ep_num, out_ep_num) + 1;
	ep_array = kcalloc(mtu->num_eps * 2, sizeof(*ep_array), GFP_KERNEL);
	if (ep_array == NULL)
		return -ENOMEM;

	mtu->ep_array = ep_array;
	mtu->in_eps = ep_array;
	mtu->out_eps = &ep_array[mtu->num_eps];
	/* ep0 uses in_eps[0], out_eps[0] is reserved */
	mtu->ep0 = mtu->in_eps;
	mtu->ep0->mtu = mtu;
	mtu->ep0->epnum = 0;

	for (i = 1; i < mtu->num_eps; i++) {
		struct mtu3_ep *mep = mtu->in_eps + i;

		mep->fifo = &mtu->tx_fifo;
		mep = mtu->out_eps + i;
		mep->fifo = &mtu->rx_fifo;
	}

	get_ep_fifo_config(mtu);

	ret = mtu3_qmu_init(mtu);
	if (ret)
		kfree(mtu->ep_array);

	return ret;
}

static void mtu3_mem_free(struct mtu3 *mtu)
{
	mtu3_qmu_exit(mtu);
	kfree(mtu->ep_array);
}

static void mtu3_set_speed(struct mtu3 *mtu)
{
	void __iomem *mbase = mtu->mac_base;

	if (!mtu->is_u3_ip && (mtu->max_speed > USB_SPEED_HIGH))
		mtu->max_speed = USB_SPEED_HIGH;

	if (mtu->max_speed == USB_SPEED_FULL) {
		/* disable U3 SS function */
		mtu3_clrbits(mbase, U3D_USB3_CONFIG, USB3_EN);
		/* disable HS function */
		mtu3_clrbits(mbase, U3D_POWER_MANAGEMENT, HS_ENABLE);
	} else if (mtu->max_speed == USB_SPEED_HIGH) {
		mtu3_clrbits(mbase, U3D_USB3_CONFIG, USB3_EN);
		/* HS/FS detected by HW */
		mtu3_setbits(mbase, U3D_POWER_MANAGEMENT, HS_ENABLE);
	} else if (mtu->max_speed == USB_SPEED_SUPER) {
		mtu3_clrbits(mtu->ippc_base, SSUSB_U3_CTRL(0),
			     SSUSB_U3_PORT_SSP_SPEED);
	}

	dev_info(mtu->dev, "max_speed: %s\n",
		usb_speed_string(mtu->max_speed));
}

static void mtu3_regs_init(struct mtu3 *mtu)
{

	void __iomem *mbase = mtu->mac_base;

	/* be sure interrupts are disabled before registration of ISR */
	mtu3_intr_disable(mtu);
	mtu3_intr_status_clear(mtu);

	if (mtu->is_u3_ip) {
		/* disable LGO_U1/U2 by default */
		mtu3_clrbits(mbase, U3D_LINK_POWER_CONTROL,
				SW_U1_ACCEPT_ENABLE | SW_U2_ACCEPT_ENABLE |
				SW_U1_REQUEST_ENABLE | SW_U2_REQUEST_ENABLE);
		/* device responses to u3_exit from host automatically */
		mtu3_clrbits(mbase, U3D_LTSSM_CTRL, SOFT_U3_EXIT_EN);
		/* automatically build U2 link when U3 detect fail */
		mtu3_setbits(mbase, U3D_USB2_TEST_MODE, U2U3_AUTO_SWITCH);
	}

	mtu3_set_speed(mtu);

	/* delay about 0.1us from detecting reset to send chirp-K */
	mtu3_clrbits(mbase, U3D_LINK_RESET_INFO, WTCHRP_MSK);
	/* U2/U3 detected by HW */
	mtu3_writel(mbase, U3D_DEVICE_CONF, 0);
	/* enable QMU 16B checksum */
	mtu3_setbits(mbase, U3D_QCR0, QMU_CS16B_EN);
	/* vbus detected by HW */
	mtu3_clrbits(mbase, U3D_MISC_CTRL, VBUS_FRC_EN | VBUS_ON);
}

static irqreturn_t mtu3_link_isr(struct mtu3 *mtu)
{
	void __iomem *mbase = mtu->mac_base;
	enum usb_device_speed udev_speed;
	u32 maxpkt = 64;
	u32 link;
	u32 speed;

	link = mtu3_readl(mbase, U3D_DEV_LINK_INTR);
	link &= mtu3_readl(mbase, U3D_DEV_LINK_INTR_ENABLE);
	mtu3_writel(mbase, U3D_DEV_LINK_INTR, link); /* W1C */
	dev_dbg(mtu->dev, "=== LINK[%x] ===\n", link);

	if (!(link & SSUSB_DEV_SPEED_CHG_INTR))
		return IRQ_NONE;

	speed = SSUSB_DEV_SPEED(mtu3_readl(mbase, U3D_DEVICE_CONF));

	switch (speed) {
	case MTU3_SPEED_FULL:
		udev_speed = USB_SPEED_FULL;
		/*BESLCK = 4 < BESLCK_U3 = 10 < BESLDCK = 15 */
		mtu3_writel(mbase, U3D_USB20_LPM_PARAMETER, LPM_BESLDCK(0xf)
				| LPM_BESLCK(4) | LPM_BESLCK_U3(0xa));
		mtu3_setbits(mbase, U3D_POWER_MANAGEMENT,
				LPM_BESL_STALL | LPM_BESLD_STALL);
		break;
	case MTU3_SPEED_HIGH:
		udev_speed = USB_SPEED_HIGH;
		/*BESLCK = 4 < BESLCK_U3 = 10 < BESLDCK = 15 */
		mtu3_writel(mbase, U3D_USB20_LPM_PARAMETER, LPM_BESLDCK(0xf)
				| LPM_BESLCK(4) | LPM_BESLCK_U3(0xa));
		mtu3_setbits(mbase, U3D_POWER_MANAGEMENT,
				LPM_BESL_STALL | LPM_BESLD_STALL);
		break;
	case MTU3_SPEED_SUPER:
		udev_speed = USB_SPEED_SUPER;
		maxpkt = 512;
		break;
	case MTU3_SPEED_SUPER_PLUS:
		udev_speed = USB_SPEED_SUPER_PLUS;
		maxpkt = 512;
		break;
	default:
		udev_speed = USB_SPEED_UNKNOWN;
		break;
	}
	dev_dbg(mtu->dev, "%s: %s\n", __func__, usb_speed_string(udev_speed));

	mtu->g.speed = udev_speed;
	mtu->g.ep0->maxpacket = maxpkt;
	mtu->ep0_state = MU3D_EP0_STATE_SETUP;

	if (udev_speed == USB_SPEED_UNKNOWN)
		mtu3_gadget_disconnect(mtu);
	else
		mtu3_ep0_setup(mtu);

	return IRQ_HANDLED;
}

static irqreturn_t mtu3_u3_ltssm_isr(struct mtu3 *mtu)
{
	void __iomem *mbase = mtu->mac_base;
	u32 ltssm;

	ltssm = mtu3_readl(mbase, U3D_LTSSM_INTR);
	ltssm &= mtu3_readl(mbase, U3D_LTSSM_INTR_ENABLE);
	mtu3_writel(mbase, U3D_LTSSM_INTR, ltssm); /* W1C */
	dev_dbg(mtu->dev, "=== LTSSM[%x] ===\n", ltssm);

	if (ltssm & (HOT_RST_INTR | WARM_RST_INTR))
		mtu3_gadget_reset(mtu);

	if (ltssm & VBUS_FALL_INTR)
		mtu3_ss_func_set(mtu, false);

	if (ltssm & VBUS_RISE_INTR)
		mtu3_ss_func_set(mtu, true);

	if (ltssm & EXIT_U3_INTR)
		mtu3_gadget_resume(mtu);

	if (ltssm & ENTER_U3_INTR)
		mtu3_gadget_suspend(mtu);

	return IRQ_HANDLED;
}

static irqreturn_t mtu3_u2_common_isr(struct mtu3 *mtu)
{
	void __iomem *mbase = mtu->mac_base;
	u32 u2comm;

	u2comm = mtu3_readl(mbase, U3D_COMMON_USB_INTR);
	u2comm &= mtu3_readl(mbase, U3D_COMMON_USB_INTR_ENABLE);
	mtu3_writel(mbase, U3D_COMMON_USB_INTR, u2comm); /* W1C */
	dev_dbg(mtu->dev, "=== U2COMM[%x] ===\n", u2comm);

	if (u2comm & SUSPEND_INTR)
		mtu3_gadget_suspend(mtu);

	if (u2comm & RESUME_INTR)
		mtu3_gadget_resume(mtu);

	if (u2comm & RESET_INTR)
		mtu3_gadget_reset(mtu);

	return IRQ_HANDLED;
}

static irqreturn_t mtu3_irq(int irq, void *data)
{
	struct mtu3 *mtu = (struct mtu3 *)data;
	unsigned long flags;
	u32 level1;

	spin_lock_irqsave(&mtu->lock, flags);

	/* U3D_LV1ISR is RU */
	level1 = mtu3_readl(mtu->mac_base, U3D_LV1ISR);
	level1 &= mtu3_readl(mtu->mac_base, U3D_LV1IER);

	if (level1 & EP_CTRL_INTR)
		mtu3_link_isr(mtu);

	if (level1 & MAC2_INTR)
		mtu3_u2_common_isr(mtu);

	if (level1 & MAC3_INTR)
		mtu3_u3_ltssm_isr(mtu);

	if (level1 & BMU_INTR)
		mtu3_ep0_isr(mtu);

	if (level1 & QMU_INTR)
		mtu3_qmu_isr(mtu);

	spin_unlock_irqrestore(&mtu->lock, flags);

	return IRQ_HANDLED;
}

static int mtu3_hw_init(struct mtu3 *mtu)
{
	u32 cap_dev;
	int ret;

	mtu->hw_version = mtu3_readl(mtu->ippc_base, U3D_SSUSB_HW_ID);

	cap_dev = mtu3_readl(mtu->ippc_base, U3D_SSUSB_IP_DEV_CAP);
	mtu->is_u3_ip = !!SSUSB_IP_DEV_U3_PORT_NUM(cap_dev);

	dev_info(mtu->dev, "IP version 0x%x(%s IP)\n", mtu->hw_version,
		mtu->is_u3_ip ? "U3" : "U2");

	mtu3_device_reset(mtu);

	ret = mtu3_device_enable(mtu);
	if (ret) {
		dev_err(mtu->dev, "device enable failed %d\n", ret);
		return ret;
	}

	ret = mtu3_mem_alloc(mtu);
	if (ret)
		return -ENOMEM;

	mtu3_regs_init(mtu);

	return 0;
}

static void mtu3_hw_exit(struct mtu3 *mtu)
{
	mtu3_device_disable(mtu);
	mtu3_mem_free(mtu);
}

/**
 * we set 32-bit DMA mask by default, here check whether the controller
 * supports 36-bit DMA or not, if it does, set 36-bit DMA mask.
 */
static int mtu3_set_dma_mask(struct mtu3 *mtu)
{
	struct device *dev = mtu->dev;
	bool is_36bit = false;
	int ret = 0;
	u32 value;

	value = mtu3_readl(mtu->mac_base, U3D_MISC_CTRL);
	if (value & DMA_ADDR_36BIT) {
		is_36bit = true;
		ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(36));
		/* If set 36-bit DMA mask fails, fall back to 32-bit DMA mask */
		if (ret) {
			is_36bit = false;
			ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
		}
	}
	dev_info(dev, "dma mask: %s bits\n", is_36bit ? "36" : "32");

	return ret;
}

int ssusb_gadget_init(struct ssusb_mtk *ssusb)
{
	struct device *dev = ssusb->dev;
	struct platform_device *pdev = to_platform_device(dev);
	struct mtu3 *mtu = NULL;
	struct resource *res;
	int ret = -ENOMEM;

	mtu = devm_kzalloc(dev, sizeof(struct mtu3), GFP_KERNEL);
	if (mtu == NULL)
		return -ENOMEM;

	mtu->irq = platform_get_irq(pdev, 0);
	if (mtu->irq < 0) {
		dev_err(dev, "fail to get irq number\n");
		return mtu->irq;
	}
	dev_info(dev, "irq %d\n", mtu->irq);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "mac");
	mtu->mac_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(mtu->mac_base)) {
		dev_err(dev, "error mapping memory for dev mac\n");
		return PTR_ERR(mtu->mac_base);
	}

	spin_lock_init(&mtu->lock);
	mtu->dev = dev;
	mtu->ippc_base = ssusb->ippc_base;
	ssusb->mac_base	= mtu->mac_base;
	ssusb->u3d = mtu;
	mtu->ssusb = ssusb;
	mtu->max_speed = usb_get_maximum_speed(dev);

	/* check the max_speed parameter */
	switch (mtu->max_speed) {
	case USB_SPEED_FULL:
	case USB_SPEED_HIGH:
	case USB_SPEED_SUPER:
	case USB_SPEED_SUPER_PLUS:
		break;
	default:
		dev_err(dev, "invalid max_speed: %s\n",
			usb_speed_string(mtu->max_speed));
		/* fall through */
	case USB_SPEED_UNKNOWN:
		/* default as SSP */
		mtu->max_speed = USB_SPEED_SUPER_PLUS;
		break;
	}

	dev_dbg(dev, "mac_base=0x%p, ippc_base=0x%p\n",
		mtu->mac_base, mtu->ippc_base);

	ret = mtu3_hw_init(mtu);
	if (ret) {
		dev_err(dev, "mtu3 hw init failed:%d\n", ret);
		return ret;
	}

	ret = mtu3_set_dma_mask(mtu);
	if (ret) {
		dev_err(dev, "mtu3 set dma_mask failed:%d\n", ret);
		goto dma_mask_err;
	}

	ret = devm_request_irq(dev, mtu->irq, mtu3_irq, 0, dev_name(dev), mtu);
	if (ret) {
		dev_err(dev, "request irq %d failed!\n", mtu->irq);
		goto irq_err;
	}

	device_init_wakeup(dev, true);

	ret = mtu3_gadget_setup(mtu);
	if (ret) {
		dev_err(dev, "mtu3 gadget init failed:%d\n", ret);
		goto gadget_err;
	}

	/* init as host mode, power down device IP for power saving */
	if (mtu->ssusb->dr_mode == USB_DR_MODE_OTG)
		mtu3_stop(mtu);

	dev_dbg(dev, " %s() done...\n", __func__);

	return 0;

gadget_err:
	device_init_wakeup(dev, false);

dma_mask_err:
irq_err:
	mtu3_hw_exit(mtu);
	ssusb->u3d = NULL;
	dev_err(dev, " %s() fail...\n", __func__);

	return ret;
}

void ssusb_gadget_exit(struct ssusb_mtk *ssusb)
{
	struct mtu3 *mtu = ssusb->u3d;

	mtu3_gadget_cleanup(mtu);
	device_init_wakeup(ssusb->dev, false);
	mtu3_hw_exit(mtu);
}
