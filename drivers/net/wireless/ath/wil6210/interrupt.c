/*
 * Copyright (c) 2012 Qualcomm Atheros, Inc.
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
#define WIL6210_IMC_RX		BIT_DMA_EP_RX_ICR_RX_DONE
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
	iowrite32(x, addr);
}
#endif /* defined(CONFIG_WIL6210_ISR_COR) */

static inline u32 wil_ioread32_and_clear(void __iomem *addr)
{
	u32 x = ioread32(addr);

	wil_icr_clear(x, addr);

	return x;
}

static void wil6210_mask_irq_tx(struct wil6210_priv *wil)
{
	iowrite32(WIL6210_IRQ_DISABLE, wil->csr +
		  HOSTADDR(RGF_DMA_EP_TX_ICR) +
		  offsetof(struct RGF_ICR, IMS));
}

static void wil6210_mask_irq_rx(struct wil6210_priv *wil)
{
	iowrite32(WIL6210_IRQ_DISABLE, wil->csr +
		  HOSTADDR(RGF_DMA_EP_RX_ICR) +
		  offsetof(struct RGF_ICR, IMS));
}

static void wil6210_mask_irq_misc(struct wil6210_priv *wil)
{
	iowrite32(WIL6210_IRQ_DISABLE, wil->csr +
		  HOSTADDR(RGF_DMA_EP_MISC_ICR) +
		  offsetof(struct RGF_ICR, IMS));
}

static void wil6210_mask_irq_pseudo(struct wil6210_priv *wil)
{
	wil_dbg_irq(wil, "%s()\n", __func__);

	iowrite32(WIL6210_IRQ_DISABLE, wil->csr +
		  HOSTADDR(RGF_DMA_PSEUDO_CAUSE_MASK_SW));

	clear_bit(wil_status_irqen, &wil->status);
}

void wil6210_unmask_irq_tx(struct wil6210_priv *wil)
{
	iowrite32(WIL6210_IMC_TX, wil->csr +
		  HOSTADDR(RGF_DMA_EP_TX_ICR) +
		  offsetof(struct RGF_ICR, IMC));
}

void wil6210_unmask_irq_rx(struct wil6210_priv *wil)
{
	iowrite32(WIL6210_IMC_RX, wil->csr +
		  HOSTADDR(RGF_DMA_EP_RX_ICR) +
		  offsetof(struct RGF_ICR, IMC));
}

static void wil6210_unmask_irq_misc(struct wil6210_priv *wil)
{
	iowrite32(WIL6210_IMC_MISC, wil->csr +
		  HOSTADDR(RGF_DMA_EP_MISC_ICR) +
		  offsetof(struct RGF_ICR, IMC));
}

static void wil6210_unmask_irq_pseudo(struct wil6210_priv *wil)
{
	wil_dbg_irq(wil, "%s()\n", __func__);

	set_bit(wil_status_irqen, &wil->status);

	iowrite32(WIL6210_IRQ_PSEUDO_MASK, wil->csr +
		  HOSTADDR(RGF_DMA_PSEUDO_CAUSE_MASK_SW));
}

void wil6210_disable_irq(struct wil6210_priv *wil)
{
	wil_dbg_irq(wil, "%s()\n", __func__);

	wil6210_mask_irq_tx(wil);
	wil6210_mask_irq_rx(wil);
	wil6210_mask_irq_misc(wil);
	wil6210_mask_irq_pseudo(wil);
}

void wil6210_enable_irq(struct wil6210_priv *wil)
{
	wil_dbg_irq(wil, "%s()\n", __func__);

	iowrite32(WIL_ICR_ICC_VALUE, wil->csr + HOSTADDR(RGF_DMA_EP_RX_ICR) +
		  offsetof(struct RGF_ICR, ICC));
	iowrite32(WIL_ICR_ICC_VALUE, wil->csr + HOSTADDR(RGF_DMA_EP_TX_ICR) +
		  offsetof(struct RGF_ICR, ICC));
	iowrite32(WIL_ICR_ICC_VALUE, wil->csr + HOSTADDR(RGF_DMA_EP_MISC_ICR) +
		  offsetof(struct RGF_ICR, ICC));

	/* interrupt moderation parameters */
	if (wil->wdev->iftype == NL80211_IFTYPE_MONITOR) {
		/* disable interrupt moderation for monitor
		 * to get better timestamp precision
		 */
		iowrite32(0, wil->csr + HOSTADDR(RGF_DMA_ITR_CNT_CRL));
	} else {
		iowrite32(WIL6210_ITR_TRSH,
			  wil->csr + HOSTADDR(RGF_DMA_ITR_CNT_TRSH));
		iowrite32(BIT_DMA_ITR_CNT_CRL_EN,
			  wil->csr + HOSTADDR(RGF_DMA_ITR_CNT_CRL));
	}

	wil6210_unmask_irq_pseudo(wil);
	wil6210_unmask_irq_tx(wil);
	wil6210_unmask_irq_rx(wil);
	wil6210_unmask_irq_misc(wil);
}

static irqreturn_t wil6210_irq_rx(int irq, void *cookie)
{
	struct wil6210_priv *wil = cookie;
	u32 isr = wil_ioread32_and_clear(wil->csr +
					 HOSTADDR(RGF_DMA_EP_RX_ICR) +
					 offsetof(struct RGF_ICR, ICR));

	trace_wil6210_irq_rx(isr);
	wil_dbg_irq(wil, "ISR RX 0x%08x\n", isr);

	if (!isr) {
		wil_err(wil, "spurious IRQ: RX\n");
		return IRQ_NONE;
	}

	wil6210_mask_irq_rx(wil);

	if (isr & BIT_DMA_EP_RX_ICR_RX_DONE) {
		wil_dbg_irq(wil, "RX done\n");
		isr &= ~BIT_DMA_EP_RX_ICR_RX_DONE;
		if (test_bit(wil_status_reset_done, &wil->status)) {
			wil_dbg_txrx(wil, "NAPI(Rx) schedule\n");
			napi_schedule(&wil->napi_rx);
		} else {
			wil_err(wil, "Got Rx interrupt while in reset\n");
		}
	}

	if (isr)
		wil_err(wil, "un-handled RX ISR bits 0x%08x\n", isr);

	/* Rx IRQ will be enabled when NAPI processing finished */

	return IRQ_HANDLED;
}

static irqreturn_t wil6210_irq_tx(int irq, void *cookie)
{
	struct wil6210_priv *wil = cookie;
	u32 isr = wil_ioread32_and_clear(wil->csr +
					 HOSTADDR(RGF_DMA_EP_TX_ICR) +
					 offsetof(struct RGF_ICR, ICR));

	trace_wil6210_irq_tx(isr);
	wil_dbg_irq(wil, "ISR TX 0x%08x\n", isr);

	if (!isr) {
		wil_err(wil, "spurious IRQ: TX\n");
		return IRQ_NONE;
	}

	wil6210_mask_irq_tx(wil);

	if (isr & BIT_DMA_EP_TX_ICR_TX_DONE) {
		wil_dbg_irq(wil, "TX done\n");
		isr &= ~BIT_DMA_EP_TX_ICR_TX_DONE;
		/* clear also all VRING interrupts */
		isr &= ~(BIT(25) - 1UL);
		if (test_bit(wil_status_reset_done, &wil->status)) {
			wil_dbg_txrx(wil, "NAPI(Tx) schedule\n");
			napi_schedule(&wil->napi_tx);
		} else {
			wil_err(wil, "Got Tx interrupt while in reset\n");
		}
	}

	if (isr)
		wil_err(wil, "un-handled TX ISR bits 0x%08x\n", isr);

	/* Tx IRQ will be enabled when NAPI processing finished */

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
		wil_err(wil, "Firmware error detected\n");
		clear_bit(wil_status_fwready, &wil->status);
		/*
		 * do not clear @isr here - we do 2-nd part in thread
		 * there, user space get notified, and it should be done
		 * in non-atomic context
		 */
	}

	if (isr & ISR_MISC_FW_READY) {
		wil_dbg_irq(wil, "IRQ: FW ready\n");
		wil_cache_mbox_regs(wil);
		set_bit(wil_status_reset_done, &wil->status);
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
		wil_notify_fw_error(wil);
		isr &= ~ISR_MISC_FW_ERROR;
		wil_fw_error_recovery(wil);
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
	if (!test_bit(wil_status_irqen, &wil->status)) {
		u32 icm_rx = wil_ioread32_and_clear(wil->csr +
				HOSTADDR(RGF_DMA_EP_RX_ICR) +
				offsetof(struct RGF_ICR, ICM));
		u32 icr_rx = wil_ioread32_and_clear(wil->csr +
				HOSTADDR(RGF_DMA_EP_RX_ICR) +
				offsetof(struct RGF_ICR, ICR));
		u32 imv_rx = ioread32(wil->csr +
				HOSTADDR(RGF_DMA_EP_RX_ICR) +
				offsetof(struct RGF_ICR, IMV));
		u32 icm_tx = wil_ioread32_and_clear(wil->csr +
				HOSTADDR(RGF_DMA_EP_TX_ICR) +
				offsetof(struct RGF_ICR, ICM));
		u32 icr_tx = wil_ioread32_and_clear(wil->csr +
				HOSTADDR(RGF_DMA_EP_TX_ICR) +
				offsetof(struct RGF_ICR, ICR));
		u32 imv_tx = ioread32(wil->csr +
				HOSTADDR(RGF_DMA_EP_TX_ICR) +
				offsetof(struct RGF_ICR, IMV));
		u32 icm_misc = wil_ioread32_and_clear(wil->csr +
				HOSTADDR(RGF_DMA_EP_MISC_ICR) +
				offsetof(struct RGF_ICR, ICM));
		u32 icr_misc = wil_ioread32_and_clear(wil->csr +
				HOSTADDR(RGF_DMA_EP_MISC_ICR) +
				offsetof(struct RGF_ICR, ICR));
		u32 imv_misc = ioread32(wil->csr +
				HOSTADDR(RGF_DMA_EP_MISC_ICR) +
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
	u32 pseudo_cause = ioread32(wil->csr + HOSTADDR(RGF_DMA_PSEUDO_CAUSE));

	/**
	 * pseudo_cause is Clear-On-Read, no need to ACK
	 */
	if ((pseudo_cause == 0) || ((pseudo_cause & 0xff) == 0xff))
		return IRQ_NONE;

	/* FIXME: IRQ mask debug */
	if (wil6210_debug_irq_mask(wil, pseudo_cause))
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

static int wil6210_request_3msi(struct wil6210_priv *wil, int irq)
{
	int rc;
	/*
	 * IRQ's are in the following order:
	 * - Tx
	 * - Rx
	 * - Misc
	 */

	rc = request_irq(irq, wil6210_irq_tx, IRQF_SHARED,
			 WIL_NAME"_tx", wil);
	if (rc)
		return rc;

	rc = request_irq(irq + 1, wil6210_irq_rx, IRQF_SHARED,
			 WIL_NAME"_rx", wil);
	if (rc)
		goto free0;

	rc = request_threaded_irq(irq + 2, wil6210_irq_misc,
				  wil6210_irq_misc_thread,
				  IRQF_SHARED, WIL_NAME"_misc", wil);
	if (rc)
		goto free1;

	return 0;
	/* error branch */
free1:
	free_irq(irq + 1, wil);
free0:
	free_irq(irq, wil);

	return rc;
}
/* can't use wil_ioread32_and_clear because ICC value is not ser yet */
static inline void wil_clear32(void __iomem *addr)
{
	u32 x = ioread32(addr);

	iowrite32(x, addr);
}

void wil6210_clear_irq(struct wil6210_priv *wil)
{
	wil_clear32(wil->csr + HOSTADDR(RGF_DMA_EP_RX_ICR) +
		    offsetof(struct RGF_ICR, ICR));
	wil_clear32(wil->csr + HOSTADDR(RGF_DMA_EP_TX_ICR) +
		    offsetof(struct RGF_ICR, ICR));
	wil_clear32(wil->csr + HOSTADDR(RGF_DMA_EP_MISC_ICR) +
		    offsetof(struct RGF_ICR, ICR));
}

int wil6210_init_irq(struct wil6210_priv *wil, int irq)
{
	int rc;
	if (wil->n_msi == 3)
		rc = wil6210_request_3msi(wil, irq);
	else
		rc = request_threaded_irq(irq, wil6210_hardirq,
					  wil6210_thread_irq,
					  wil->n_msi ? 0 : IRQF_SHARED,
					  WIL_NAME, wil);
	if (rc)
		return rc;

	wil6210_enable_irq(wil);

	return 0;
}

void wil6210_fini_irq(struct wil6210_priv *wil, int irq)
{
	wil6210_disable_irq(wil);
	free_irq(irq, wil);
	if (wil->n_msi == 3) {
		free_irq(irq + 1, wil);
		free_irq(irq + 2, wil);
	}
}
