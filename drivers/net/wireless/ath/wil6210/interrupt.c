/*
 * Copyright (c) 2012-2015 Qualcomm Atheros, Inc.
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

#define WIL6210_IRQ_DISABLE	(0xFFFFFFFFUL)
#define WIL6210_IMC_RX		(BIT_DMA_EP_RX_ICR_RX_DONE | \
				 BIT_DMA_EP_RX_ICR_RX_HTRSH)
#define WIL6210_IMC_TX		(BIT_DMA_EP_TX_ICR_TX_DONE | \
				BIT_DMA_EP_TX_ICR_TX_DONE_N(0))
#define WIL6210_IMC_MISC	(ISR_MISC_FW_READY | \
				 ISR_MISC_MBOX_EVT | \
				 ISR_MISC_FW_ERROR)

#define WIL6210_IRQ_PSEUDO_MASK (u32)(~(BIT_DMA_PSEUDO_CAUSE_RX | \
					BIT_DMA_PSEUDO_CAUSE_TX | \
					BIT_DMA_PSEUDO_CAUSE_MISC))

#if defined(CONFIG_WIL6210_ISR_COR)
/* configure to Clear-On-Read mode */
#define WIL_ICR_ICC_VALUE	(0xFFFFFFFFUL)

static inline void wil_icr_clear(u32 x, void __iomem *addr)
{
}
#else /* defined(CONFIG_WIL6210_ISR_COR) */
/* configure to Write-1-to-Clear mode */
#define WIL_ICR_ICC_VALUE	(0UL)

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

static void wil6210_mask_irq_rx(struct wil6210_priv *wil)
{
	wil_w(wil, RGF_DMA_EP_RX_ICR + offsetof(struct RGF_ICR, IMS),
	      WIL6210_IRQ_DISABLE);
}

static void wil6210_mask_irq_misc(struct wil6210_priv *wil)
{
	wil_w(wil, RGF_DMA_EP_MISC_ICR + offsetof(struct RGF_ICR, IMS),
	      WIL6210_IRQ_DISABLE);
}

static void wil6210_mask_irq_pseudo(struct wil6210_priv *wil)
{
	wil_dbg_irq(wil, "%s()\n", __func__);

	wil_w(wil, RGF_DMA_PSEUDO_CAUSE_MASK_SW, WIL6210_IRQ_DISABLE);

	clear_bit(wil_status_irqen, wil->status);
}

void wil6210_unmask_irq_tx(struct wil6210_priv *wil)
{
	wil_w(wil, RGF_DMA_EP_TX_ICR + offsetof(struct RGF_ICR, IMC),
	      WIL6210_IMC_TX);
}

void wil6210_unmask_irq_rx(struct wil6210_priv *wil)
{
	wil_w(wil, RGF_DMA_EP_RX_ICR + offsetof(struct RGF_ICR, IMC),
	      WIL6210_IMC_RX);
}

static void wil6210_unmask_irq_misc(struct wil6210_priv *wil)
{
	wil_w(wil, RGF_DMA_EP_MISC_ICR + offsetof(struct RGF_ICR, IMC),
	      WIL6210_IMC_MISC);
}

static void wil6210_unmask_irq_pseudo(struct wil6210_priv *wil)
{
	wil_dbg_irq(wil, "%s()\n", __func__);

	set_bit(wil_status_irqen, wil->status);

	wil_w(wil, RGF_DMA_PSEUDO_CAUSE_MASK_SW, WIL6210_IRQ_PSEUDO_MASK);
}

void wil_mask_irq(struct wil6210_priv *wil)
{
	wil_dbg_irq(wil, "%s()\n", __func__);

	wil6210_mask_irq_tx(wil);
	wil6210_mask_irq_rx(wil);
	wil6210_mask_irq_misc(wil);
	wil6210_mask_irq_pseudo(wil);
}

void wil_unmask_irq(struct wil6210_priv *wil)
{
	wil_dbg_irq(wil, "%s()\n", __func__);

	wil_w(wil, RGF_DMA_EP_RX_ICR + offsetof(struct RGF_ICR, ICC),
	      WIL_ICR_ICC_VALUE);
	wil_w(wil, RGF_DMA_EP_TX_ICR + offsetof(struct RGF_ICR, ICC),
	      WIL_ICR_ICC_VALUE);
	wil_w(wil, RGF_DMA_EP_MISC_ICR + offsetof(struct RGF_ICR, ICC),
	      WIL_ICR_ICC_VALUE);

	wil6210_unmask_irq_pseudo(wil);
	wil6210_unmask_irq_tx(wil);
	wil6210_unmask_irq_rx(wil);
	wil6210_unmask_irq_misc(wil);
}

void wil_configure_interrupt_moderation(struct wil6210_priv *wil)
{
	wil_dbg_irq(wil, "%s()\n", __func__);

	/* disable interrupt moderation for monitor
	 * to get better timestamp precision
	 */
	if (wil->wdev->iftype == NL80211_IFTYPE_MONITOR)
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
		wil_err(wil, "spurious IRQ: RX\n");
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
		wil_dbg_irq(wil, "RX done\n");

		if (unlikely(isr & BIT_DMA_EP_RX_ICR_RX_HTRSH))
			wil_err_ratelimited(wil,
					    "Received \"Rx buffer is in risk of overflow\" interrupt\n");

		isr &= ~(BIT_DMA_EP_RX_ICR_RX_DONE |
			 BIT_DMA_EP_RX_ICR_RX_HTRSH);
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
		wil6210_unmask_irq_rx(wil);

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
		wil_err(wil, "spurious IRQ: TX\n");
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
			wil_err(wil, "Got Tx interrupt while in reset\n");
		}
	}

	if (unlikely(isr))
		wil_err(wil, "un-handled TX ISR bits 0x%08x\n", isr);

	/* Tx IRQ will be enabled when NAPI processing finished */

	atomic_inc(&wil->isr_count_tx);

	if (unlikely(need_unmask))
		wil6210_unmask_irq_tx(wil);

	return IRQ_HANDLED;
}

static void wil_notify_fw_error(struct wil6210_priv *wil)
{
	struct device *dev = &wil_to_ndev(wil)->dev;
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

	wil6210_mask_irq_misc(wil);

	if (isr & ISR_MISC_FW_ERROR) {
		u32 fw_assert_code = wil_r(wil, RGF_FW_ASSERT_CODE);
		u32 ucode_assert_code = wil_r(wil, RGF_UCODE_ASSERT_CODE);

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
		set_bit(wil_status_mbox_ready, wil->status);
		/**
		 * Actual FW ready indicated by the
		 * WMI_FW_READY_EVENTID
		 */
		isr &= ~ISR_MISC_FW_READY;
	}

	wil->isr_misc = isr;

	if (isr) {
		return IRQ_WAKE_THREAD;
	} else {
		wil6210_unmask_irq_misc(wil);
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

	wil6210_unmask_irq_misc(wil);

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
	if (!test_bit(wil_status_irqen, wil->status)) {
		u32 icm_rx = wil_ioread32_and_clear(wil->csr +
				HOSTADDR(RGF_DMA_EP_RX_ICR) +
				offsetof(struct RGF_ICR, ICM));
		u32 icr_rx = wil_ioread32_and_clear(wil->csr +
				HOSTADDR(RGF_DMA_EP_RX_ICR) +
				offsetof(struct RGF_ICR, ICR));
		u32 imv_rx = wil_r(wil, RGF_DMA_EP_RX_ICR +
				   offsetof(struct RGF_ICR, IMV));
		u32 icm_tx = wil_ioread32_and_clear(wil->csr +
				HOSTADDR(RGF_DMA_EP_TX_ICR) +
				offsetof(struct RGF_ICR, ICM));
		u32 icr_tx = wil_ioread32_and_clear(wil->csr +
				HOSTADDR(RGF_DMA_EP_TX_ICR) +
				offsetof(struct RGF_ICR, ICR));
		u32 imv_tx = wil_r(wil, RGF_DMA_EP_TX_ICR +
				   offsetof(struct RGF_ICR, IMV));
		u32 icm_misc = wil_ioread32_and_clear(wil->csr +
				HOSTADDR(RGF_DMA_EP_MISC_ICR) +
				offsetof(struct RGF_ICR, ICM));
		u32 icr_misc = wil_ioread32_and_clear(wil->csr +
				HOSTADDR(RGF_DMA_EP_MISC_ICR) +
				offsetof(struct RGF_ICR, ICR));
		u32 imv_misc = wil_r(wil, RGF_DMA_EP_MISC_ICR +
				     offsetof(struct RGF_ICR, IMV));
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

	/* FIXME: IRQ mask debug */
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
	    (wil6210_irq_rx(irq, cookie) == IRQ_WAKE_THREAD))
		rc = IRQ_WAKE_THREAD;

	if ((pseudo_cause & BIT_DMA_PSEUDO_CAUSE_TX) &&
	    (wil6210_irq_tx(irq, cookie) == IRQ_WAKE_THREAD))
		rc = IRQ_WAKE_THREAD;

	if ((pseudo_cause & BIT_DMA_PSEUDO_CAUSE_MISC) &&
	    (wil6210_irq_misc(irq, cookie) == IRQ_WAKE_THREAD))
		rc = IRQ_WAKE_THREAD;

	/* if thread is requested, it will unmask IRQ */
	if (rc != IRQ_WAKE_THREAD)
		wil6210_unmask_irq_pseudo(wil);

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
	wil_clear32(wil->csr + HOSTADDR(RGF_DMA_EP_MISC_ICR) +
		    offsetof(struct RGF_ICR, ICR));
	wmb(); /* make sure write completed */
}

int wil6210_init_irq(struct wil6210_priv *wil, int irq, bool use_msi)
{
	int rc;

	wil_dbg_misc(wil, "%s(%s)\n", __func__, use_msi ? "MSI" : "INTx");

	rc = request_threaded_irq(irq, wil6210_hardirq,
				  wil6210_thread_irq,
				  use_msi ? 0 : IRQF_SHARED,
				  WIL_NAME, wil);
	return rc;
}

void wil6210_fini_irq(struct wil6210_priv *wil, int irq)
{
	wil_dbg_misc(wil, "%s()\n", __func__);

	wil_mask_irq(wil);
	free_irq(irq, wil);
}
