/*
 * Copyright (c) 2012-2017 Qualcomm Atheros, Inc.
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <linux/interrupt.h>

#include "wil6210.h"
#include "trace.h"

/**
 * Theory of operation:
 *
 * There is ISR pseudo-cause register,
 * dma_rgf->DMA_RGF.PSEUDO_CAUSE.PSEUDO_CAUSE
 * Its bits represents OR'ed bits from 3 real ISR registers:
 * TX, RX, and MISC.
 *
 * Registers may be configured to either "write 1 to clear" or
 * "clear on read" mode
 *
 * When handling interrupt, one have to mask/unmask interrupts for the
 * real ISR registers, or hardware may malfunction.
 *
 */

#define WIL6210_IRQ_DISABLE		(0xFFFFFFFFUL)
#define WIL6210_IRQ_DISABLE_NO_HALP	(0xF7FFFFFFUL)
#define WIL6210_IMC_RX		(BIT_DMA_EP_RX_ICR_RX_DONE | \
				 BIT_DMA_EP_RX_ICR_RX_HTRSH)
#define WIL6210_IMC_RX_NO_RX_HTRSH (WIL6210_IMC_RX & \
				    (~(BIT_DMA_EP_RX_ICR_RX_HTRSH)))
#define WIL6210_IMC_TX		(BIT_DMA_EP_TX_ICR_TX_DONE | \
				BIT_DMA_EP_TX_ICR_TX_DONE_N(0))
#define WIL6210_IMC_TX_EDMA		BIT_TX_STATUS_IRQ
#define WIL6210_IMC_RX_EDMA		BIT_RX_STATUS_IRQ
#define WIL6210_IMC_MISC_NO_HALP	(ISR_MISC_FW_READY | \
					 ISR_MISC_MBOX_EVT | \
					 ISR_MISC_FW_ERROR)
#define WIL6210_IMC_MISC		(WIL6210_IMC_MISC_NO_HALP | \
					 BIT_DMA_EP_MISC_ICR_HALP)
#define WIL6210_IRQ_PSEUDO_MASK (u32)(~(BIT_DMA_PSEUDO_CAUSE_RX | \
					BIT_DMA_PSEUDO_CAUSE_TX | \
					BIT_DMA_PSEUDO_CAUSE_MISC))

#if defined(CONFIG_WIL6210_ISR_COR)
/* configure to Clear-On-Read mode */
#define WIL_ICR_ICC_VALUE	(0xFFFFFFFFUL)
#define WIL_ICR_ICC_MISC_VALUE	(0xF7FFFFFFUL)

static inline void wil_icr_clear(u32 x, void __iomem *addr)
{
}
#else /* defined(CONFIG_WIL6210_ISR_COR) */
/* configure to Write-1-to-Clear mode */
#define WIL_ICR_ICC_VALUE	(0UL)
#define WIL_ICR_ICC_MISC_VALUE	(0UL)

static inline void wil_icr_clear(u32 x, void __iomem *addr)
{
	writel(x, addr);
}
#endif /* defined(CONFIG_WIL6210_ISR_COR) */

static inline u32 wil_ioread32_and_clear(void __iomem *addr)
{
	u32 x = readl(addr);

	wil_icr_clear(x, addr);

	return x;
}

static void wil6210_mask_irq_tx(struct wil6210_priv *wil)
{
	wil_w(wil, RGF_DMA_EP_TX_ICR + offsetof(struct RGF_ICR, IMS),
	      WIL6210_IRQ_DISABLE);
}

static void wil6210_mask_irq_tx_edma(struct wil6210_priv *wil)
{
	wil_w(wil, RGF_INT_GEN_TX_ICR + offsetof(struct RGF_ICR, IMS),
	      WIL6210_IRQ_DISABLE);
}

static void wil6210_mask_irq_rx(struct wil6210_priv *wil)
{
	wil_w(wil, RGF_DMA_EP_RX_ICR + offsetof(struct RGF_ICR, IMS),
	      WIL6210_IRQ_DISABLE);
}

static void wil6210_mask_irq_rx_edma(struct wil6210_priv *wil)
{
	wil_w(wil, RGF_INT_GEN_RX_ICR + offsetof(struct RGF_ICR, IMS),
	      WIL6210_IRQ_DISABLE);
}

static void wil6210_mask_irq_misc(struct wil6210_priv *wil, bool mask_halp)
{
	wil_dbg_irq(wil, "mask_irq_misc: mask_halp(%s)\n",
		    mask_halp ? "true" : "false");

	wil_w(wil, RGF_DMA_EP_MISC_ICR + offsetof(struct RGF_ICR, IMS),
	      mask_halp ? WIL6210_IRQ_DISABLE : WIL6210_IRQ_DISABLE_NO_HALP);
}

void wil6210_mask_halp(struct wil6210_priv *wil)
{
	wil_dbg_irq(wil, "mask_halp\n");

	wil_w(wil, RGF_DMA_EP_MISC_ICR + offsetof(struct RGF_ICR, IMS),
	      BIT_DMA_EP_MISC_ICR_HALP);
}

static void wil6210_mask_irq_pseudo(struct wil6210_priv *wil)
{
	wil_dbg_irq(wil, "mask_irq_pseudo\n");

	wil_w(wil, RGF_DMA_PSEUDO_CAUSE_MASK_SW, WIL6210_IRQ_DISABLE);

	clear_bit(wil_status_irqen, wil->status);
}

void wil6210_unmask_irq_tx(struct wil6210_priv *wil)
{
	wil_w(wil, RGF_DMA_EP_TX_ICR + offsetof(struct RGF_ICR, IMC),
	      WIL6210_IMC_TX);
}

void wil6210_unmask_irq_tx_edma(struct wil6210_priv *wil)
{
	wil_w(wil, RGF_INT_GEN_TX_ICR + offsetof(struct RGF_ICR, IMC),
	      WIL6210_IMC_TX_EDMA);
}

void wil6210_unmask_irq_rx(struct wil6210_priv *wil)
{
	bool unmask_rx_htrsh = atomic_read(&wil->connected_vifs) > 0;

	wil_w(wil, RGF_DMA_EP_RX_ICR + offsetof(struct RGF_ICR, IMC),
	      unmask_rx_htrsh ? WIL6210_IMC_RX : WIL6210_IMC_RX_NO_RX_HTRSH);
}

void wil6210_unmask_irq_rx_edma(struct wil6210_priv *wil)
{
	wil_w(wil, RGF_INT_GEN_RX_ICR + offsetof(struct RGF_ICR, IMC),
	      WIL6210_IMC_RX_EDMA);
}

static void wil6210_unmask_irq_misc(struct wil6210_priv *wil, bool unmask_halp)
{
	wil_dbg_irq(wil, "unmask_irq_misc: unmask_halp(%s)\n",
		    unmask_halp ? "true" : "false");

	wil_w(wil, RGF_DMA_EP_MISC_ICR + offsetof(struct RGF_ICR, IMC),
	      unmask_halp ? WIL6210_IMC_MISC : WIL6210_IMC_MISC_NO_HALP);
}

static void wil6210_unmask_halp(struct wil6210_priv *wil)
{
	wil_dbg_irq(wil, "unmask_halp\n");

	wil_w(wil, RGF_DMA_EP_MISC_ICR + offsetof(struct RGF_ICR, IMC),
	      BIT_DMA_EP_MISC_ICR_HALP);
}

static void wil6210_unmask_irq_pseudo(struct wil6210_priv *wil)
{
	wil_dbg_irq(wil, "unmask_irq_pseudo\n");

	set_bit(wil_status_irqen, wil->status);

	wil_w(wil, RGF_DMA_PSEUDO_CAUSE_MASK_SW, WIL6210_IRQ_PSEUDO_MASK);
}

void wil_mask_irq(struct wil6210_priv *wil)
{
	wil_dbg_irq(wil, "mask_irq\n");

	wil6210_mask_irq_tx(wil);
	wil6210_mask_irq_tx_edma(wil);
	wil6210_mask_irq_rx(wil);
	wil6210_mask_irq_rx_edma(wil);
	wil6210_mask_irq_misc(wil, true);
	wil6210_mask_irq_pseudo(wil);
}

void wil_unmask_irq(struct wil6210_priv *wil)
{
	wil_dbg_irq(wil, "unmask_irq\n");

	wil_w(wil, RGF_DMA_EP_RX_ICR + offsetof(struct RGF_ICR, ICC),
	      WIL_ICR_ICC_VALUE);
	wil_w(wil, RGF_DMA_EP_TX_ICR + offsetof(struct RGF_ICR, ICC),
	      WIL_ICR_ICC_VALUE);
	wil_w(wil, RGF_DMA_EP_MISC_ICR + offsetof(struct RGF_ICR, ICC),
	      WIL_ICR_ICC_MISC_VALUE);
	wil_w(wil, RGF_INT_GEN_TX_ICR + offsetof(struct RGF_ICR, ICC),
	      WIL_ICR_ICC_VALUE);
	wil_w(wil, RGF_INT_GEN_RX_ICR + offsetof(struct RGF_ICR, ICC),
	      WIL_ICR_ICC_VALUE);

	wil6210_unmask_irq_pseudo(wil);
	if (wil->use_enhanced_dma_hw) {
		wil6210_unmask_irq_tx_edma(wil);
		wil6210_unmask_irq_rx_edma(wil);
	} else {
		wil6210_unmask_irq_tx(wil);
		wil6210_unmask_irq_rx(wil);
	}
	wil6210_unmask_irq_misc(wil, true);
}

void wil_configure_interrupt_moderation_edma(struct wil6210_priv *wil)
{
	u32 moderation;

	wil_s(wil, RGF_INT_GEN_IDLE_TIME_LIMIT, WIL_EDMA_IDLE_TIME_LIMIT_USEC);

	wil_s(wil, RGF_INT_GEN_TIME_UNIT_LIMIT, WIL_EDMA_TIME_UNIT_CLK_CYCLES);

	/* Update RX and TX moderation */
	moderation = wil->rx_max_burst_duration |
		(WIL_EDMA_AGG_WATERMARK << WIL_EDMA_AGG_WATERMARK_POS);
	wil_w(wil, RGF_INT_CTRL_INT_GEN_CFG_0, moderation);
	wil_w(wil, RGF_INT_CTRL_INT_GEN_CFG_1, moderation);

	/* Treat special events as regular
	 * (set bit 0 to 0x1 and clear bits 1-8)
	 */
	wil_c(wil, RGF_INT_COUNT_ON_SPECIAL_EVT, 0x1FE);
	wil_s(wil, RGF_INT_COUNT_ON_SPECIAL_EVT, 0x1);
}

void wil_configure_interrupt_moderation(struct wil6210_priv *wil)
{
	struct wireless_dev *wdev = wil->main_ndev->ieee80211_ptr;

	wil_dbg_irq(wil, "configure_interrupt_moderation\n");

	/* disable interrupt moderation for monitor
	 * to get better timestamp precision
	 */
	if (wdev->iftype == NL80211_IFTYPE_MONITOR)
		return;

	/* Disable and clear tx counter before (re)configuration */
	wil_w(wil, RGF_DMA_ITR_TX_CNT_CTL, BIT_DMA_ITR_TX_CNT_CTL_CLR);
	wil_w(wil, RGF_DMA_ITR_TX_CNT_TRSH, wil->tx_max_burst_duration);
	wil_info(wil, "set ITR_TX_CNT_TRSH = %d usec\n",
		 wil->tx_max_burst_duration);
	/* Configure TX max burst duration timer to use usec units */
	wil_w(wil, RGF_DMA_ITR_TX_CNT_CTL,
	      BIT_DMA_ITR_TX_CNT_CTL_EN | BIT_DMA_ITR_TX_CNT_CTL_EXT_TIC_SEL);

	/* Disable and clear tx idle counter before (re)configuration */
	wil_w(wil, RGF_DMA_ITR_TX_IDL_CNT_CTL, BIT_DMA_ITR_TX_IDL_CNT_CTL_CLR);
	wil_w(wil, RGF_DMA_ITR_TX_IDL_CNT_TRSH, wil->tx_interframe_timeout);
	wil_info(wil, "set ITR_TX_IDL_CNT_TRSH = %d usec\n",
		 wil->tx_interframe_timeout);
	/* Configure TX max burst duration timer to use usec units */
	wil_w(wil, RGF_DMA_ITR_TX_IDL_CNT_CTL, BIT_DMA_ITR_TX_IDL_CNT_CTL_EN |
	      BIT_DMA_ITR_TX_IDL_CNT_CTL_EXT_TIC_SEL);

	/* Disable and clear rx counter before (re)configuration */
	wil_w(wil, RGF_DMA_ITR_RX_CNT_CTL, BIT_DMA_ITR_RX_CNT_CTL_CLR);
	wil_w(wil, RGF_DMA_ITR_RX_CNT_TRSH, wil->rx_max_burst_duration);
	wil_info(wil, "set ITR_RX_CNT_TRSH = %d usec\n",
		 wil->rx_max_burst_duration);
	/* Configure TX max burst duration timer to use usec units */
	wil_w(wil, RGF_DMA_ITR_RX_CNT_CTL,
	      BIT_DMA_ITR_RX_CNT_CTL_EN | BIT_DMA_ITR_RX_CNT_CTL_EXT_TIC_SEL);

	/* Disable and clear rx idle counter before (re)configuration */
	wil_w(wil, RGF_DMA_ITR_RX_IDL_CNT_CTL, BIT_DMA_ITR_RX_IDL_CNT_CTL_CLR);
	wil_w(wil, RGF_DMA_ITR_RX_IDL_CNT_TRSH, wil->rx_interframe_timeout);
	wil_info(wil, "set ITR_RX_IDL_CNT_TRSH = %d usec\n",
		 wil->rx_interframe_timeout);
	/* Configure TX max burst duration timer to use usec units */
	wil_w(wil, RGF_DMA_ITR_RX_IDL_CNT_CTL, BIT_DMA_ITR_RX_IDL_CNT_CTL_EN |
	      BIT_DMA_ITR_RX_IDL_CNT_CTL_EXT_TIC_SEL);
}

static irqreturn_t wil6210_irq_rx(int irq, void *cookie)
{
	struct wil6210_priv *wil = cookie;
	u32 isr = wil_ioread32_and_clear(wil->csr +
					 HOSTADDR(RGF_DMA_EP_RX_ICR) +
					 offsetof(struct RGF_ICR, ICR));
	bool need_unmask = true;

	trace_wil6210_irq_rx(isr);
	wil_dbg_irq(wil, "ISR RX 0x%08x\n", isr);

	if (unlikely(!isr)) {
		wil_err_ratelimited(wil, "spurious IRQ: RX\n");
		return IRQ_NONE;
	}

	wil6210_mask_irq_rx(wil);

	/* RX_DONE and RX_HTRSH interrupts are the same if interrupt
	 * moderation is not used. Interrupt moderation may cause RX
	 * buffer overflow while RX_DONE is delayed. The required
	 * action is always the same - should empty the accumulated
	 * packets from the RX ring.
	 */
	if (likely(isr & (BIT_DMA_EP_RX_ICR_RX_DONE |
			  BIT_DMA_EP_RX_ICR_RX_HTRSH))) {
		wil_dbg_irq(wil, "RX done / RX_HTRSH received, ISR (0x%x)\n",
			    isr);

		isr &= ~(BIT_DMA_EP_RX_ICR_RX_DONE |
			 BIT_DMA_EP_RX_ICR_RX_HTRSH);
		if (likely(test_bit(wil_status_fwready, wil->status))) {
			if (likely(test_bit(wil_status_napi_en, wil->status))) {
				wil_dbg_txrx(wil, "NAPI(Rx) schedule\n");
				need_unmask = false;
				napi_schedule(&wil->napi_rx);
			} else {
				wil_err_ratelimited(
					wil,
					"Got Rx interrupt while stopping interface\n");
			}
		} else {
			wil_err_ratelimited(wil, "Got Rx interrupt while in reset\n");
		}
	}

	if (unlikely(isr))
		wil_err(wil, "un-handled RX ISR bits 0x%08x\n", isr);

	/* Rx IRQ will be enabled when NAPI processing finished */

	atomic_inc(&wil->isr_count_rx);

	if (unlikely(need_unmask))
		wil6210_unmask_irq_rx(wil);

	return IRQ_HANDLED;
}

static irqreturn_t wil6210_irq_rx_edma(int irq, void *cookie)
{
	struct wil6210_priv *wil = cookie;
	u32 isr = wil_ioread32_and_clear(wil->csr +
					 HOSTADDR(RGF_INT_GEN_RX_ICR) +
					 offsetof(struct RGF_ICR, ICR));
	bool need_unmask = true;

	trace_wil6210_irq_rx(isr);
	wil_dbg_irq(wil, "ISR RX 0x%08x\n", isr);

	if (unlikely(!isr)) {
		wil_err(wil, "spurious IRQ: RX\n");
		return IRQ_NONE;
	}

	wil6210_mask_irq_rx_edma(wil);

	if (likely(isr & BIT_RX_STATUS_IRQ)) {
		wil_dbg_irq(wil, "RX status ring\n");
		isr &= ~BIT_RX_STATUS_IRQ;
		if (likely(test_bit(wil_status_fwready, wil->status))) {
			if (likely(test_bit(wil_status_napi_en, wil->status))) {
				wil_dbg_txrx(wil, "NAPI(Rx) schedule\n");
				need_unmask = false;
				napi_schedule(&wil->napi_rx);
			} else {
				wil_err(wil,
					"Got Rx interrupt while stopping interface\n");
			}
		} else {
			wil_err(wil, "Got Rx interrupt while in reset\n");
		}
	}

	if (unlikely(isr))
		wil_err(wil, "un-handled RX ISR bits 0x%08x\n", isr);

	/* Rx IRQ will be enabled when NAPI processing finished */

	atomic_inc(&wil->isr_count_rx);

	if (unlikely(need_unmask))
		wil6210_unmask_irq_rx_edma(wil);

	return IRQ_HANDLED;
}

static irqreturn_t wil6210_irq_tx_edma(int irq, void *cookie)
{
	struct wil6210_priv *wil = cookie;
	u32 isr = wil_ioread32_and_clear(wil->csr +
					 HOSTADDR(RGF_INT_GEN_TX_ICR) +
					 offsetof(struct RGF_ICR, ICR));
	bool need_unmask = true;

	trace_wil6210_irq_tx(isr);
	wil_dbg_irq(wil, "ISR TX 0x%08x\n", isr);

	if (unlikely(!isr)) {
		wil_err(wil, "spurious IRQ: TX\n");
		return IRQ_NONE;
	}

	wil6210_mask_irq_tx_edma(wil);

	if (likely(isr & BIT_TX_STATUS_IRQ)) {
		wil_dbg_irq(wil, "TX status ring\n");
		isr &= ~BIT_TX_STATUS_IRQ;
		if (likely(test_bit(wil_status_fwready, wil->status))) {
			wil_dbg_txrx(wil, "NAPI(Tx) schedule\n");
			need_unmask = false;
			napi_schedule(&wil->napi_tx);
		} else {
			wil_err(wil, "Got Tx status ring IRQ while in reset\n");
		}
	}

	if (unlikely(isr))
		wil_err(wil, "un-handled TX ISR bits 0x%08x\n", isr);

	/* Tx IRQ will be enabled when NAPI processing finished */

	atomic_inc(&wil->isr_count_tx);

	if (unlikely(need_unmask))
		wil6210_unmask_irq_tx_edma(wil);

	return IRQ_HANDLED;
}

static irqreturn_t wil6210_irq_tx(int irq, void *cookie)
{
	struct wil6210_priv *wil = cookie;
	u32 isr = wil_ioread32_and_clear(wil->csr +
					 HOSTADDR(RGF_DMA_EP_TX_ICR) +
					 offsetof(struct RGF_ICR, ICR));
	bool need_unmask = true;

	trace_wil6210_irq_tx(isr);
	wil_dbg_irq(wil, "ISR TX 0x%08x\n", isr);

	if (unlikely(!isr)) {
		wil_err_ratelimited(wil, "spurious IRQ: TX\n");
		return IRQ_NONE;
	}

	wil6210_mask_irq_tx(wil);

	if (likely(isr & BIT_DMA_EP_TX_ICR_TX_DONE)) {
		wil_dbg_irq(wil, "TX done\n");
		isr &= ~BIT_DMA_EP_TX_ICR_TX_DONE;
		/* clear also all VRING interrupts */
		isr &= ~(BIT(25) - 1UL);
		if (likely(test_bit(wil_status_fwready, wil->status))) {
			wil_dbg_txrx(wil, "NAPI(Tx) schedule\n");
			need_unmask = false;
			napi_schedule(&wil->napi_tx);
		} else {
			wil_err_ratelimited(wil, "Got Tx interrupt while in reset\n");
		}
	}

	if (unlikely(isr))
		wil_err_ratelimited(wil, "un-handled TX ISR bits 0x%08x\n",
				    isr);

	/* Tx IRQ will be enabled when NAPI processing finished */

	atomic_inc(&wil->isr_count_tx);

	if (unlikely(need_unmask))
		wil6210_unmask_irq_tx(wil);

	return IRQ_HANDLED;
}

static void wil_notify_fw_error(struct wil6210_priv *wil)
{
	struct device *dev = &wil->main_ndev->dev;
	char *envp[3] = {
		[0] = "SOURCE=wil6210",
		[1] = "EVENT=FW_ERROR",
		[2] = NULL,
	};
	wil_err(wil, "Notify about firmware error\n");
	kobject_uevent_env(&dev->kobj, KOBJ_CHANGE, envp);
}

static void wil_cache_mbox_regs(struct wil6210_priv *wil)
{
	/* make shadow copy of registers that should not change on run time */
	wil_memcpy_fromio_32(&wil->mbox_ctl, wil->csr + HOST_MBOX,
			     sizeof(struct wil6210_mbox_ctl));
	wil_mbox_ring_le2cpus(&wil->mbox_ctl.rx);
	wil_mbox_ring_le2cpus(&wil->mbox_ctl.tx);
}

static bool wil_validate_mbox_regs(struct wil6210_priv *wil)
{
	size_t min_size = sizeof(struct wil6210_mbox_hdr) +
		sizeof(struct wmi_cmd_hdr);

	if (wil->mbox_ctl.rx.entry_size < min_size) {
		wil_err(wil, "rx mbox entry too small (%d)\n",
			wil->mbox_ctl.rx.entry_size);
		return false;
	}
	if (wil->mbox_ctl.tx.entry_size < min_size) {
		wil_err(wil, "tx mbox entry too small (%d)\n",
			wil->mbox_ctl.tx.entry_size);
		return false;
	}

	return true;
}

static irqreturn_t wil6210_irq_misc(int irq, void *cookie)
{
	struct wil6210_priv *wil = cookie;
	u32 isr = wil_ioread32_and_clear(wil->csr +
					 HOSTADDR(RGF_DMA_EP_MISC_ICR) +
					 offsetof(struct RGF_ICR, ICR));

	trace_wil6210_irq_misc(isr);
	wil_dbg_irq(wil, "ISR MISC 0x%08x\n", isr);

	if (!isr) {
		wil_err(wil, "spurious IRQ: MISC\n");
		return IRQ_NONE;
	}

	wil6210_mask_irq_misc(wil, false);

	if (isr & ISR_MISC_FW_ERROR) {
		u32 fw_assert_code = wil_r(wil, wil->rgf_fw_assert_code_addr);
		u32 ucode_assert_code =
			wil_r(wil, wil->rgf_ucode_assert_code_addr);

		wil_err(wil,
			"Firmware error detected, assert codes FW 0x%08x, UCODE 0x%08x\n",
			fw_assert_code, ucode_assert_code);
		clear_bit(wil_status_fwready, wil->status);
		/*
		 * do not clear @isr here - we do 2-nd part in thread
		 * there, user space get notified, and it should be done
		 * in non-atomic context
		 */
	}

	if (isr & ISR_MISC_FW_READY) {
		wil_dbg_irq(wil, "IRQ: FW ready\n");
		wil_cache_mbox_regs(wil);
		if (wil_validate_mbox_regs(wil))
			set_bit(wil_status_mbox_ready, wil->status);
		/**
		 * Actual FW ready indicated by the
		 * WMI_FW_READY_EVENTID
		 */
		isr &= ~ISR_MISC_FW_READY;
	}

	if (isr & BIT_DMA_EP_MISC_ICR_HALP) {
		wil_dbg_irq(wil, "irq_misc: HALP IRQ invoked\n");
		wil6210_mask_halp(wil);
		isr &= ~BIT_DMA_EP_MISC_ICR_HALP;
		complete(&wil->halp.comp);
	}

	wil->isr_misc = isr;

	if (isr) {
		return IRQ_WAKE_THREAD;
	} else {
		wil6210_unmask_irq_misc(wil, false);
		return IRQ_HANDLED;
	}
}

static irqreturn_t wil6210_irq_misc_thread(int irq, void *cookie)
{
	struct wil6210_priv *wil = cookie;
	u32 isr = wil->isr_misc;

	trace_wil6210_irq_misc_thread(isr);
	wil_dbg_irq(wil, "Thread ISR MISC 0x%08x\n", isr);

	if (isr & ISR_MISC_FW_ERROR) {
		wil->recovery_state = fw_recovery_pending;
		wil_fw_core_dump(wil);
		wil_notify_fw_error(wil);
		isr &= ~ISR_MISC_FW_ERROR;
		if (wil->platform_ops.notify) {
			wil_err(wil, "notify platform driver about FW crash");
			wil->platform_ops.notify(wil->platform_handle,
						 WIL_PLATFORM_EVT_FW_CRASH);
		} else {
			wil_fw_error_recovery(wil);
		}
	}
	if (isr & ISR_MISC_MBOX_EVT) {
		wil_dbg_irq(wil, "MBOX event\n");
		wmi_recv_cmd(wil);
		isr &= ~ISR_MISC_MBOX_EVT;
	}

	if (isr)
		wil_dbg_irq(wil, "un-handled MISC ISR bits 0x%08x\n", isr);

	wil->isr_misc = 0;

	wil6210_unmask_irq_misc(wil, false);

	/* in non-triple MSI case, this is done inside wil6210_thread_irq
	 * because it has to be done after unmasking the pseudo.
	 */
	if (wil->n_msi == 3 && wil->suspend_resp_rcvd) {
		wil_dbg_irq(wil, "set suspend_resp_comp to true\n");
		wil->suspend_resp_comp = true;
		wake_up_interruptible(&wil->wq);
	}

	return IRQ_HANDLED;
}

/**
 * thread IRQ handler
 */
static irqreturn_t wil6210_thread_irq(int irq, void *cookie)
{
	struct wil6210_priv *wil = cookie;

	wil_dbg_irq(wil, "Thread IRQ\n");
	/* Discover real IRQ cause */
	if (wil->isr_misc)
		wil6210_irq_misc_thread(irq, cookie);

	wil6210_unmask_irq_pseudo(wil);

	if (wil->suspend_resp_rcvd) {
		wil_dbg_irq(wil, "set suspend_resp_comp to true\n");
		wil->suspend_resp_comp = true;
		wake_up_interruptible(&wil->wq);
	}

	return IRQ_HANDLED;
}

/* DEBUG
 * There is subtle bug in hardware that causes IRQ to raise when it should be
 * masked. It is quite rare and hard to debug.
 *
 * Catch irq issue if it happens and print all I can.
 */
static int wil6210_debug_irq_mask(struct wil6210_priv *wil, u32 pseudo_cause)
{
	u32 icm_rx, icr_rx, imv_rx;
	u32 icm_tx, icr_tx, imv_tx;
	u32 icm_misc, icr_misc, imv_misc;

	if (!test_bit(wil_status_irqen, wil->status)) {
		if (wil->use_enhanced_dma_hw) {
			icm_rx = wil_ioread32_and_clear(wil->csr +
					HOSTADDR(RGF_INT_GEN_RX_ICR) +
					offsetof(struct RGF_ICR, ICM));
			icr_rx = wil_ioread32_and_clear(wil->csr +
					HOSTADDR(RGF_INT_GEN_RX_ICR) +
					offsetof(struct RGF_ICR, ICR));
			imv_rx = wil_r(wil, RGF_INT_GEN_RX_ICR +
				   offsetof(struct RGF_ICR, IMV));
			icm_tx = wil_ioread32_and_clear(wil->csr +
					HOSTADDR(RGF_INT_GEN_TX_ICR) +
					offsetof(struct RGF_ICR, ICM));
			icr_tx = wil_ioread32_and_clear(wil->csr +
					HOSTADDR(RGF_INT_GEN_TX_ICR) +
					offsetof(struct RGF_ICR, ICR));
			imv_tx = wil_r(wil, RGF_INT_GEN_TX_ICR +
					   offsetof(struct RGF_ICR, IMV));
		} else {
			icm_rx = wil_ioread32_and_clear(wil->csr +
					HOSTADDR(RGF_DMA_EP_RX_ICR) +
					offsetof(struct RGF_ICR, ICM));
			icr_rx = wil_ioread32_and_clear(wil->csr +
					HOSTADDR(RGF_DMA_EP_RX_ICR) +
					offsetof(struct RGF_ICR, ICR));
			imv_rx = wil_r(wil, RGF_DMA_EP_RX_ICR +
				   offsetof(struct RGF_ICR, IMV));
			icm_tx = wil_ioread32_and_clear(wil->csr +
					HOSTADDR(RGF_DMA_EP_TX_ICR) +
					offsetof(struct RGF_ICR, ICM));
			icr_tx = wil_ioread32_and_clear(wil->csr +
					HOSTADDR(RGF_DMA_EP_TX_ICR) +
					offsetof(struct RGF_ICR, ICR));
			imv_tx = wil_r(wil, RGF_DMA_EP_TX_ICR +
					   offsetof(struct RGF_ICR, IMV));
		}
		icm_misc = wil_ioread32_and_clear(wil->csr +
				HOSTADDR(RGF_DMA_EP_MISC_ICR) +
				offsetof(struct RGF_ICR, ICM));
		icr_misc = wil_ioread32_and_clear(wil->csr +
				HOSTADDR(RGF_DMA_EP_MISC_ICR) +
				offsetof(struct RGF_ICR, ICR));
		imv_misc = wil_r(wil, RGF_DMA_EP_MISC_ICR +
				     offsetof(struct RGF_ICR, IMV));

		/* HALP interrupt can be unmasked when misc interrupts are
		 * masked
		 */
		if (icr_misc & BIT_DMA_EP_MISC_ICR_HALP)
			return 0;

		wil_err(wil, "IRQ when it should be masked: pseudo 0x%08x\n"
				"Rx   icm:icr:imv 0x%08x 0x%08x 0x%08x\n"
				"Tx   icm:icr:imv 0x%08x 0x%08x 0x%08x\n"
				"Misc icm:icr:imv 0x%08x 0x%08x 0x%08x\n",
				pseudo_cause,
				icm_rx, icr_rx, imv_rx,
				icm_tx, icr_tx, imv_tx,
				icm_misc, icr_misc, imv_misc);

		return -EINVAL;
	}

	return 0;
}

static irqreturn_t wil6210_hardirq(int irq, void *cookie)
{
	irqreturn_t rc = IRQ_HANDLED;
	struct wil6210_priv *wil = cookie;
	u32 pseudo_cause = wil_r(wil, RGF_DMA_PSEUDO_CAUSE);

	/**
	 * pseudo_cause is Clear-On-Read, no need to ACK
	 */
	if (unlikely((pseudo_cause == 0) || ((pseudo_cause & 0xff) == 0xff)))
		return IRQ_NONE;

	/* IRQ mask debug */
	if (unlikely(wil6210_debug_irq_mask(wil, pseudo_cause)))
		return IRQ_NONE;

	trace_wil6210_irq_pseudo(pseudo_cause);
	wil_dbg_irq(wil, "Pseudo IRQ 0x%08x\n", pseudo_cause);

	wil6210_mask_irq_pseudo(wil);

	/* Discover real IRQ cause
	 * There are 2 possible phases for every IRQ:
	 * - hard IRQ handler called right here
	 * - threaded handler called later
	 *
	 * Hard IRQ handler reads and clears ISR.
	 *
	 * If threaded handler requested, hard IRQ handler
	 * returns IRQ_WAKE_THREAD and saves ISR register value
	 * for the threaded handler use.
	 *
	 * voting for wake thread - need at least 1 vote
	 */
	if ((pseudo_cause & BIT_DMA_PSEUDO_CAUSE_RX) &&
	    (wil->txrx_ops.irq_rx(irq, cookie) == IRQ_WAKE_THREAD))
		rc = IRQ_WAKE_THREAD;

	if ((pseudo_cause & BIT_DMA_PSEUDO_CAUSE_TX) &&
	    (wil->txrx_ops.irq_tx(irq, cookie) == IRQ_WAKE_THREAD))
		rc = IRQ_WAKE_THREAD;

	if ((pseudo_cause & BIT_DMA_PSEUDO_CAUSE_MISC) &&
	    (wil6210_irq_misc(irq, cookie) == IRQ_WAKE_THREAD))
		rc = IRQ_WAKE_THREAD;

	/* if thread is requested, it will unmask IRQ */
	if (rc != IRQ_WAKE_THREAD)
		wil6210_unmask_irq_pseudo(wil);

	return rc;
}

static int wil6210_request_3msi(struct wil6210_priv *wil, int irq)
{
	int rc;

	/* IRQ's are in the following order:
	 * - Tx
	 * - Rx
	 * - Misc
	 */
	rc = request_irq(irq, wil->txrx_ops.irq_tx, IRQF_SHARED,
			 WIL_NAME "_tx", wil);
	if (rc)
		return rc;

	rc = request_irq(irq + 1, wil->txrx_ops.irq_rx, IRQF_SHARED,
			 WIL_NAME "_rx", wil);
	if (rc)
		goto free0;

	rc = request_threaded_irq(irq + 2, wil6210_irq_misc,
				  wil6210_irq_misc_thread,
				  IRQF_SHARED, WIL_NAME "_misc", wil);
	if (rc)
		goto free1;

	return 0;
free1:
	free_irq(irq + 1, wil);
free0:
	free_irq(irq, wil);

	return rc;
}

/* can't use wil_ioread32_and_clear because ICC value is not set yet */
static inline void wil_clear32(void __iomem *addr)
{
	u32 x = readl(addr);

	writel(x, addr);
}

void wil6210_clear_irq(struct wil6210_priv *wil)
{
	wil_clear32(wil->csr + HOSTADDR(RGF_DMA_EP_RX_ICR) +
		    offsetof(struct RGF_ICR, ICR));
	wil_clear32(wil->csr + HOSTADDR(RGF_DMA_EP_TX_ICR) +
		    offsetof(struct RGF_ICR, ICR));
	wil_clear32(wil->csr + HOSTADDR(RGF_INT_GEN_RX_ICR) +
		    offsetof(struct RGF_ICR, ICR));
	wil_clear32(wil->csr + HOSTADDR(RGF_INT_GEN_TX_ICR) +
		    offsetof(struct RGF_ICR, ICR));
	wil_clear32(wil->csr + HOSTADDR(RGF_DMA_EP_MISC_ICR) +
		    offsetof(struct RGF_ICR, ICR));
	wmb(); /* make sure write completed */
}

void wil6210_set_halp(struct wil6210_priv *wil)
{
	wil_dbg_irq(wil, "set_halp\n");

	wil_w(wil, RGF_DMA_EP_MISC_ICR + offsetof(struct RGF_ICR, ICS),
	      BIT_DMA_EP_MISC_ICR_HALP);
}

void wil6210_clear_halp(struct wil6210_priv *wil)
{
	wil_dbg_irq(wil, "clear_halp\n");

	wil_w(wil, RGF_DMA_EP_MISC_ICR + offsetof(struct RGF_ICR, ICR),
	      BIT_DMA_EP_MISC_ICR_HALP);
	wil6210_unmask_halp(wil);
}

int wil6210_init_irq(struct wil6210_priv *wil, int irq)
{
	int rc;

	wil_dbg_misc(wil, "init_irq: %s, n_msi=%d\n",
		     wil->n_msi ? "MSI" : "INTx", wil->n_msi);

	if (wil->use_enhanced_dma_hw) {
		wil->txrx_ops.irq_tx = wil6210_irq_tx_edma;
		wil->txrx_ops.irq_rx = wil6210_irq_rx_edma;
	} else {
		wil->txrx_ops.irq_tx = wil6210_irq_tx;
		wil->txrx_ops.irq_rx = wil6210_irq_rx;
	}

	if (wil->n_msi == 3)
		rc = wil6210_request_3msi(wil, irq);
	else
		rc = request_threaded_irq(irq, wil6210_hardirq,
					  wil6210_thread_irq,
					  wil->n_msi ? 0 : IRQF_SHARED,
					  WIL_NAME, wil);
	return rc;
}

void wil6210_fini_irq(struct wil6210_priv *wil, int irq)
{
	wil_dbg_misc(wil, "fini_irq:\n");

	wil_mask_irq(wil);
	free_irq(irq, wil);
	if (wil->n_msi == 3) {
		free_irq(irq + 1, wil);
		free_irq(irq + 2, wil);
	}
}
