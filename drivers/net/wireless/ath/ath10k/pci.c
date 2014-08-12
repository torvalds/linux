/*
 * Copyright (c) 2005-2011 Atheros Communications Inc.
 * Copyright (c) 2011-2013 Qualcomm Atheros, Inc.
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

#include <linux/pci.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/bitops.h>

#include "core.h"
#include "debug.h"

#include "targaddrs.h"
#include "bmi.h"

#include "hif.h"
#include "htc.h"

#include "ce.h"
#include "pci.h"

enum ath10k_pci_irq_mode {
	ATH10K_PCI_IRQ_AUTO = 0,
	ATH10K_PCI_IRQ_LEGACY = 1,
	ATH10K_PCI_IRQ_MSI = 2,
};

enum ath10k_pci_reset_mode {
	ATH10K_PCI_RESET_AUTO = 0,
	ATH10K_PCI_RESET_WARM_ONLY = 1,
};

static unsigned int ath10k_pci_irq_mode = ATH10K_PCI_IRQ_AUTO;
static unsigned int ath10k_pci_reset_mode = ATH10K_PCI_RESET_AUTO;

module_param_named(irq_mode, ath10k_pci_irq_mode, uint, 0644);
MODULE_PARM_DESC(irq_mode, "0: auto, 1: legacy, 2: msi (default: 0)");

module_param_named(reset_mode, ath10k_pci_reset_mode, uint, 0644);
MODULE_PARM_DESC(reset_mode, "0: auto, 1: warm only (default: 0)");

/* how long wait to wait for target to initialise, in ms */
#define ATH10K_PCI_TARGET_WAIT 3000
#define ATH10K_PCI_NUM_WARM_RESET_ATTEMPTS 3

#define QCA988X_2_0_DEVICE_ID	(0x003c)

static DEFINE_PCI_DEVICE_TABLE(ath10k_pci_id_table) = {
	{ PCI_VDEVICE(ATHEROS, QCA988X_2_0_DEVICE_ID) }, /* PCI-E QCA988X V2 */
	{0}
};

static int ath10k_pci_diag_read_access(struct ath10k *ar, u32 address,
				       u32 *data);

static int ath10k_pci_post_rx(struct ath10k *ar);
static int ath10k_pci_post_rx_pipe(struct ath10k_pci_pipe *pipe_info,
					     int num);
static void ath10k_pci_rx_pipe_cleanup(struct ath10k_pci_pipe *pipe_info);
static int ath10k_pci_cold_reset(struct ath10k *ar);
static int ath10k_pci_warm_reset(struct ath10k *ar);
static int ath10k_pci_wait_for_target_init(struct ath10k *ar);
static int ath10k_pci_init_irq(struct ath10k *ar);
static int ath10k_pci_deinit_irq(struct ath10k *ar);
static int ath10k_pci_request_irq(struct ath10k *ar);
static void ath10k_pci_free_irq(struct ath10k *ar);
static int ath10k_pci_bmi_wait(struct ath10k_ce_pipe *tx_pipe,
			       struct ath10k_ce_pipe *rx_pipe,
			       struct bmi_xfer *xfer);

static const struct ce_attr host_ce_config_wlan[] = {
	/* CE0: host->target HTC control and raw streams */
	{
		.flags = CE_ATTR_FLAGS,
		.src_nentries = 16,
		.src_sz_max = 256,
		.dest_nentries = 0,
	},

	/* CE1: target->host HTT + HTC control */
	{
		.flags = CE_ATTR_FLAGS,
		.src_nentries = 0,
		.src_sz_max = 512,
		.dest_nentries = 512,
	},

	/* CE2: target->host WMI */
	{
		.flags = CE_ATTR_FLAGS,
		.src_nentries = 0,
		.src_sz_max = 2048,
		.dest_nentries = 32,
	},

	/* CE3: host->target WMI */
	{
		.flags = CE_ATTR_FLAGS,
		.src_nentries = 32,
		.src_sz_max = 2048,
		.dest_nentries = 0,
	},

	/* CE4: host->target HTT */
	{
		.flags = CE_ATTR_FLAGS | CE_ATTR_DIS_INTR,
		.src_nentries = CE_HTT_H2T_MSG_SRC_NENTRIES,
		.src_sz_max = 256,
		.dest_nentries = 0,
	},

	/* CE5: unused */
	{
		.flags = CE_ATTR_FLAGS,
		.src_nentries = 0,
		.src_sz_max = 0,
		.dest_nentries = 0,
	},

	/* CE6: target autonomous hif_memcpy */
	{
		.flags = CE_ATTR_FLAGS,
		.src_nentries = 0,
		.src_sz_max = 0,
		.dest_nentries = 0,
	},

	/* CE7: ce_diag, the Diagnostic Window */
	{
		.flags = CE_ATTR_FLAGS,
		.src_nentries = 2,
		.src_sz_max = DIAG_TRANSFER_LIMIT,
		.dest_nentries = 2,
	},
};

/* Target firmware's Copy Engine configuration. */
static const struct ce_pipe_config target_ce_config_wlan[] = {
	/* CE0: host->target HTC control and raw streams */
	{
		.pipenum = 0,
		.pipedir = PIPEDIR_OUT,
		.nentries = 32,
		.nbytes_max = 256,
		.flags = CE_ATTR_FLAGS,
		.reserved = 0,
	},

	/* CE1: target->host HTT + HTC control */
	{
		.pipenum = 1,
		.pipedir = PIPEDIR_IN,
		.nentries = 32,
		.nbytes_max = 512,
		.flags = CE_ATTR_FLAGS,
		.reserved = 0,
	},

	/* CE2: target->host WMI */
	{
		.pipenum = 2,
		.pipedir = PIPEDIR_IN,
		.nentries = 32,
		.nbytes_max = 2048,
		.flags = CE_ATTR_FLAGS,
		.reserved = 0,
	},

	/* CE3: host->target WMI */
	{
		.pipenum = 3,
		.pipedir = PIPEDIR_OUT,
		.nentries = 32,
		.nbytes_max = 2048,
		.flags = CE_ATTR_FLAGS,
		.reserved = 0,
	},

	/* CE4: host->target HTT */
	{
		.pipenum = 4,
		.pipedir = PIPEDIR_OUT,
		.nentries = 256,
		.nbytes_max = 256,
		.flags = CE_ATTR_FLAGS,
		.reserved = 0,
	},

	/* NB: 50% of src nentries, since tx has 2 frags */

	/* CE5: unused */
	{
		.pipenum = 5,
		.pipedir = PIPEDIR_OUT,
		.nentries = 32,
		.nbytes_max = 2048,
		.flags = CE_ATTR_FLAGS,
		.reserved = 0,
	},

	/* CE6: Reserved for target autonomous hif_memcpy */
	{
		.pipenum = 6,
		.pipedir = PIPEDIR_INOUT,
		.nentries = 32,
		.nbytes_max = 4096,
		.flags = CE_ATTR_FLAGS,
		.reserved = 0,
	},

	/* CE7 used only by Host */
};

static bool ath10k_pci_irq_pending(struct ath10k *ar)
{
	u32 cause;

	/* Check if the shared legacy irq is for us */
	cause = ath10k_pci_read32(ar, SOC_CORE_BASE_ADDRESS +
				  PCIE_INTR_CAUSE_ADDRESS);
	if (cause & (PCIE_INTR_FIRMWARE_MASK | PCIE_INTR_CE_MASK_ALL))
		return true;

	return false;
}

static void ath10k_pci_disable_and_clear_legacy_irq(struct ath10k *ar)
{
	/* IMPORTANT: INTR_CLR register has to be set after
	 * INTR_ENABLE is set to 0, otherwise interrupt can not be
	 * really cleared. */
	ath10k_pci_write32(ar, SOC_CORE_BASE_ADDRESS + PCIE_INTR_ENABLE_ADDRESS,
			   0);
	ath10k_pci_write32(ar, SOC_CORE_BASE_ADDRESS + PCIE_INTR_CLR_ADDRESS,
			   PCIE_INTR_FIRMWARE_MASK | PCIE_INTR_CE_MASK_ALL);

	/* IMPORTANT: this extra read transaction is required to
	 * flush the posted write buffer. */
	(void) ath10k_pci_read32(ar, SOC_CORE_BASE_ADDRESS +
				 PCIE_INTR_ENABLE_ADDRESS);
}

static void ath10k_pci_enable_legacy_irq(struct ath10k *ar)
{
	ath10k_pci_write32(ar, SOC_CORE_BASE_ADDRESS +
			   PCIE_INTR_ENABLE_ADDRESS,
			   PCIE_INTR_FIRMWARE_MASK | PCIE_INTR_CE_MASK_ALL);

	/* IMPORTANT: this extra read transaction is required to
	 * flush the posted write buffer. */
	(void) ath10k_pci_read32(ar, SOC_CORE_BASE_ADDRESS +
				 PCIE_INTR_ENABLE_ADDRESS);
}

static irqreturn_t ath10k_pci_early_irq_handler(int irq, void *arg)
{
	struct ath10k *ar = arg;
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);

	if (ar_pci->num_msi_intrs == 0) {
		if (!ath10k_pci_irq_pending(ar))
			return IRQ_NONE;

		ath10k_pci_disable_and_clear_legacy_irq(ar);
	}

	tasklet_schedule(&ar_pci->early_irq_tasklet);

	return IRQ_HANDLED;
}

static int ath10k_pci_request_early_irq(struct ath10k *ar)
{
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
	int ret;

	/* Regardless whether MSI-X/MSI/legacy irqs have been set up the first
	 * interrupt from irq vector is triggered in all cases for FW
	 * indication/errors */
	ret = request_irq(ar_pci->pdev->irq, ath10k_pci_early_irq_handler,
			  IRQF_SHARED, "ath10k_pci (early)", ar);
	if (ret) {
		ath10k_warn("failed to request early irq: %d\n", ret);
		return ret;
	}

	return 0;
}

static void ath10k_pci_free_early_irq(struct ath10k *ar)
{
	free_irq(ath10k_pci_priv(ar)->pdev->irq, ar);
}

/*
 * Diagnostic read/write access is provided for startup/config/debug usage.
 * Caller must guarantee proper alignment, when applicable, and single user
 * at any moment.
 */
static int ath10k_pci_diag_read_mem(struct ath10k *ar, u32 address, void *data,
				    int nbytes)
{
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
	int ret = 0;
	u32 buf;
	unsigned int completed_nbytes, orig_nbytes, remaining_bytes;
	unsigned int id;
	unsigned int flags;
	struct ath10k_ce_pipe *ce_diag;
	/* Host buffer address in CE space */
	u32 ce_data;
	dma_addr_t ce_data_base = 0;
	void *data_buf = NULL;
	int i;

	/*
	 * This code cannot handle reads to non-memory space. Redirect to the
	 * register read fn but preserve the multi word read capability of
	 * this fn
	 */
	if (address < DRAM_BASE_ADDRESS) {
		if (!IS_ALIGNED(address, 4) ||
		    !IS_ALIGNED((unsigned long)data, 4))
			return -EIO;

		while ((nbytes >= 4) &&  ((ret = ath10k_pci_diag_read_access(
					   ar, address, (u32 *)data)) == 0)) {
			nbytes -= sizeof(u32);
			address += sizeof(u32);
			data += sizeof(u32);
		}
		return ret;
	}

	ce_diag = ar_pci->ce_diag;

	/*
	 * Allocate a temporary bounce buffer to hold caller's data
	 * to be DMA'ed from Target. This guarantees
	 *   1) 4-byte alignment
	 *   2) Buffer in DMA-able space
	 */
	orig_nbytes = nbytes;
	data_buf = (unsigned char *)dma_alloc_coherent(ar->dev,
						       orig_nbytes,
						       &ce_data_base,
						       GFP_ATOMIC);

	if (!data_buf) {
		ret = -ENOMEM;
		goto done;
	}
	memset(data_buf, 0, orig_nbytes);

	remaining_bytes = orig_nbytes;
	ce_data = ce_data_base;
	while (remaining_bytes) {
		nbytes = min_t(unsigned int, remaining_bytes,
			       DIAG_TRANSFER_LIMIT);

		ret = ath10k_ce_recv_buf_enqueue(ce_diag, NULL, ce_data);
		if (ret != 0)
			goto done;

		/* Request CE to send from Target(!) address to Host buffer */
		/*
		 * The address supplied by the caller is in the
		 * Target CPU virtual address space.
		 *
		 * In order to use this address with the diagnostic CE,
		 * convert it from Target CPU virtual address space
		 * to CE address space
		 */
		address = TARG_CPU_SPACE_TO_CE_SPACE(ar, ar_pci->mem,
						     address);

		ret = ath10k_ce_send(ce_diag, NULL, (u32)address, nbytes, 0,
				 0);
		if (ret)
			goto done;

		i = 0;
		while (ath10k_ce_completed_send_next(ce_diag, NULL, &buf,
						     &completed_nbytes,
						     &id) != 0) {
			mdelay(1);
			if (i++ > DIAG_ACCESS_CE_TIMEOUT_MS) {
				ret = -EBUSY;
				goto done;
			}
		}

		if (nbytes != completed_nbytes) {
			ret = -EIO;
			goto done;
		}

		if (buf != (u32) address) {
			ret = -EIO;
			goto done;
		}

		i = 0;
		while (ath10k_ce_completed_recv_next(ce_diag, NULL, &buf,
						     &completed_nbytes,
						     &id, &flags) != 0) {
			mdelay(1);

			if (i++ > DIAG_ACCESS_CE_TIMEOUT_MS) {
				ret = -EBUSY;
				goto done;
			}
		}

		if (nbytes != completed_nbytes) {
			ret = -EIO;
			goto done;
		}

		if (buf != ce_data) {
			ret = -EIO;
			goto done;
		}

		remaining_bytes -= nbytes;
		address += nbytes;
		ce_data += nbytes;
	}

done:
	if (ret == 0) {
		/* Copy data from allocated DMA buf to caller's buf */
		WARN_ON_ONCE(orig_nbytes & 3);
		for (i = 0; i < orig_nbytes / sizeof(__le32); i++) {
			((u32 *)data)[i] =
				__le32_to_cpu(((__le32 *)data_buf)[i]);
		}
	} else
		ath10k_warn("failed to read diag value at 0x%x: %d\n",
			    address, ret);

	if (data_buf)
		dma_free_coherent(ar->dev, orig_nbytes, data_buf,
				  ce_data_base);

	return ret;
}

/* Read 4-byte aligned data from Target memory or register */
static int ath10k_pci_diag_read_access(struct ath10k *ar, u32 address,
				       u32 *data)
{
	/* Assume range doesn't cross this boundary */
	if (address >= DRAM_BASE_ADDRESS)
		return ath10k_pci_diag_read_mem(ar, address, data, sizeof(u32));

	*data = ath10k_pci_read32(ar, address);
	return 0;
}

static int ath10k_pci_diag_write_mem(struct ath10k *ar, u32 address,
				     const void *data, int nbytes)
{
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
	int ret = 0;
	u32 buf;
	unsigned int completed_nbytes, orig_nbytes, remaining_bytes;
	unsigned int id;
	unsigned int flags;
	struct ath10k_ce_pipe *ce_diag;
	void *data_buf = NULL;
	u32 ce_data;	/* Host buffer address in CE space */
	dma_addr_t ce_data_base = 0;
	int i;

	ce_diag = ar_pci->ce_diag;

	/*
	 * Allocate a temporary bounce buffer to hold caller's data
	 * to be DMA'ed to Target. This guarantees
	 *   1) 4-byte alignment
	 *   2) Buffer in DMA-able space
	 */
	orig_nbytes = nbytes;
	data_buf = (unsigned char *)dma_alloc_coherent(ar->dev,
						       orig_nbytes,
						       &ce_data_base,
						       GFP_ATOMIC);
	if (!data_buf) {
		ret = -ENOMEM;
		goto done;
	}

	/* Copy caller's data to allocated DMA buf */
	WARN_ON_ONCE(orig_nbytes & 3);
	for (i = 0; i < orig_nbytes / sizeof(__le32); i++)
		((__le32 *)data_buf)[i] = __cpu_to_le32(((u32 *)data)[i]);

	/*
	 * The address supplied by the caller is in the
	 * Target CPU virtual address space.
	 *
	 * In order to use this address with the diagnostic CE,
	 * convert it from
	 *    Target CPU virtual address space
	 * to
	 *    CE address space
	 */
	address = TARG_CPU_SPACE_TO_CE_SPACE(ar, ar_pci->mem, address);

	remaining_bytes = orig_nbytes;
	ce_data = ce_data_base;
	while (remaining_bytes) {
		/* FIXME: check cast */
		nbytes = min_t(int, remaining_bytes, DIAG_TRANSFER_LIMIT);

		/* Set up to receive directly into Target(!) address */
		ret = ath10k_ce_recv_buf_enqueue(ce_diag, NULL, address);
		if (ret != 0)
			goto done;

		/*
		 * Request CE to send caller-supplied data that
		 * was copied to bounce buffer to Target(!) address.
		 */
		ret = ath10k_ce_send(ce_diag, NULL, (u32) ce_data,
				     nbytes, 0, 0);
		if (ret != 0)
			goto done;

		i = 0;
		while (ath10k_ce_completed_send_next(ce_diag, NULL, &buf,
						     &completed_nbytes,
						     &id) != 0) {
			mdelay(1);

			if (i++ > DIAG_ACCESS_CE_TIMEOUT_MS) {
				ret = -EBUSY;
				goto done;
			}
		}

		if (nbytes != completed_nbytes) {
			ret = -EIO;
			goto done;
		}

		if (buf != ce_data) {
			ret = -EIO;
			goto done;
		}

		i = 0;
		while (ath10k_ce_completed_recv_next(ce_diag, NULL, &buf,
						     &completed_nbytes,
						     &id, &flags) != 0) {
			mdelay(1);

			if (i++ > DIAG_ACCESS_CE_TIMEOUT_MS) {
				ret = -EBUSY;
				goto done;
			}
		}

		if (nbytes != completed_nbytes) {
			ret = -EIO;
			goto done;
		}

		if (buf != address) {
			ret = -EIO;
			goto done;
		}

		remaining_bytes -= nbytes;
		address += nbytes;
		ce_data += nbytes;
	}

done:
	if (data_buf) {
		dma_free_coherent(ar->dev, orig_nbytes, data_buf,
				  ce_data_base);
	}

	if (ret != 0)
		ath10k_warn("failed to write diag value at 0x%x: %d\n",
			    address, ret);

	return ret;
}

/* Write 4B data to Target memory or register */
static int ath10k_pci_diag_write_access(struct ath10k *ar, u32 address,
					u32 data)
{
	/* Assume range doesn't cross this boundary */
	if (address >= DRAM_BASE_ADDRESS)
		return ath10k_pci_diag_write_mem(ar, address, &data,
						 sizeof(u32));

	ath10k_pci_write32(ar, address, data);
	return 0;
}

static bool ath10k_pci_is_awake(struct ath10k *ar)
{
	u32 val = ath10k_pci_reg_read32(ar, RTC_STATE_ADDRESS);

	return RTC_STATE_V_GET(val) == RTC_STATE_V_ON;
}

static int ath10k_pci_wake_wait(struct ath10k *ar)
{
	int tot_delay = 0;
	int curr_delay = 5;

	while (tot_delay < PCIE_WAKE_TIMEOUT) {
		if (ath10k_pci_is_awake(ar))
			return 0;

		udelay(curr_delay);
		tot_delay += curr_delay;

		if (curr_delay < 50)
			curr_delay += 5;
	}

	return -ETIMEDOUT;
}

static int ath10k_pci_wake(struct ath10k *ar)
{
	ath10k_pci_reg_write32(ar, PCIE_SOC_WAKE_ADDRESS,
			       PCIE_SOC_WAKE_V_MASK);
	return ath10k_pci_wake_wait(ar);
}

static void ath10k_pci_sleep(struct ath10k *ar)
{
	ath10k_pci_reg_write32(ar, PCIE_SOC_WAKE_ADDRESS,
			       PCIE_SOC_WAKE_RESET);
}

/* Called by lower (CE) layer when a send to Target completes. */
static void ath10k_pci_ce_send_done(struct ath10k_ce_pipe *ce_state)
{
	struct ath10k *ar = ce_state->ar;
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
	struct ath10k_hif_cb *cb = &ar_pci->msg_callbacks_current;
	void *transfer_context;
	u32 ce_data;
	unsigned int nbytes;
	unsigned int transfer_id;

	while (ath10k_ce_completed_send_next(ce_state, &transfer_context,
					     &ce_data, &nbytes,
					     &transfer_id) == 0) {
		/* no need to call tx completion for NULL pointers */
		if (transfer_context == NULL)
			continue;

		cb->tx_completion(ar, transfer_context, transfer_id);
	}
}

/* Called by lower (CE) layer when data is received from the Target. */
static void ath10k_pci_ce_recv_data(struct ath10k_ce_pipe *ce_state)
{
	struct ath10k *ar = ce_state->ar;
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
	struct ath10k_pci_pipe *pipe_info =  &ar_pci->pipe_info[ce_state->id];
	struct ath10k_hif_cb *cb = &ar_pci->msg_callbacks_current;
	struct sk_buff *skb;
	void *transfer_context;
	u32 ce_data;
	unsigned int nbytes, max_nbytes;
	unsigned int transfer_id;
	unsigned int flags;
	int err, num_replenish = 0;

	while (ath10k_ce_completed_recv_next(ce_state, &transfer_context,
					     &ce_data, &nbytes, &transfer_id,
					     &flags) == 0) {
		num_replenish++;
		skb = transfer_context;
		max_nbytes = skb->len + skb_tailroom(skb);
		dma_unmap_single(ar->dev, ATH10K_SKB_CB(skb)->paddr,
				 max_nbytes, DMA_FROM_DEVICE);

		if (unlikely(max_nbytes < nbytes)) {
			ath10k_warn("rxed more than expected (nbytes %d, max %d)",
				    nbytes, max_nbytes);
			dev_kfree_skb_any(skb);
			continue;
		}

		skb_put(skb, nbytes);
		cb->rx_completion(ar, skb, pipe_info->pipe_num);
	}

	err = ath10k_pci_post_rx_pipe(pipe_info, num_replenish);
	if (unlikely(err)) {
		/* FIXME: retry */
		ath10k_warn("failed to replenish CE rx ring %d (%d bufs): %d\n",
			    pipe_info->pipe_num, num_replenish, err);
	}
}

static int ath10k_pci_hif_tx_sg(struct ath10k *ar, u8 pipe_id,
				struct ath10k_hif_sg_item *items, int n_items)
{
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
	struct ath10k_pci_pipe *pci_pipe = &ar_pci->pipe_info[pipe_id];
	struct ath10k_ce_pipe *ce_pipe = pci_pipe->ce_hdl;
	struct ath10k_ce_ring *src_ring = ce_pipe->src_ring;
	unsigned int nentries_mask;
	unsigned int sw_index;
	unsigned int write_index;
	int err, i = 0;

	spin_lock_bh(&ar_pci->ce_lock);

	nentries_mask = src_ring->nentries_mask;
	sw_index = src_ring->sw_index;
	write_index = src_ring->write_index;

	if (unlikely(CE_RING_DELTA(nentries_mask,
				   write_index, sw_index - 1) < n_items)) {
		err = -ENOBUFS;
		goto err;
	}

	for (i = 0; i < n_items - 1; i++) {
		ath10k_dbg(ATH10K_DBG_PCI,
			   "pci tx item %d paddr 0x%08x len %d n_items %d\n",
			   i, items[i].paddr, items[i].len, n_items);
		ath10k_dbg_dump(ATH10K_DBG_PCI_DUMP, NULL, "item data: ",
				items[i].vaddr, items[i].len);

		err = ath10k_ce_send_nolock(ce_pipe,
					    items[i].transfer_context,
					    items[i].paddr,
					    items[i].len,
					    items[i].transfer_id,
					    CE_SEND_FLAG_GATHER);
		if (err)
			goto err;
	}

	/* `i` is equal to `n_items -1` after for() */

	ath10k_dbg(ATH10K_DBG_PCI,
		   "pci tx item %d paddr 0x%08x len %d n_items %d\n",
		   i, items[i].paddr, items[i].len, n_items);
	ath10k_dbg_dump(ATH10K_DBG_PCI_DUMP, NULL, "item data: ",
			items[i].vaddr, items[i].len);

	err = ath10k_ce_send_nolock(ce_pipe,
				    items[i].transfer_context,
				    items[i].paddr,
				    items[i].len,
				    items[i].transfer_id,
				    0);
	if (err)
		goto err;

	spin_unlock_bh(&ar_pci->ce_lock);
	return 0;

err:
	for (; i > 0; i--)
		__ath10k_ce_send_revert(ce_pipe);

	spin_unlock_bh(&ar_pci->ce_lock);
	return err;
}

static u16 ath10k_pci_hif_get_free_queue_number(struct ath10k *ar, u8 pipe)
{
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);

	ath10k_dbg(ATH10K_DBG_PCI, "pci hif get free queue number\n");

	return ath10k_ce_num_free_src_entries(ar_pci->pipe_info[pipe].ce_hdl);
}

static void ath10k_pci_hif_dump_area(struct ath10k *ar)
{
	u32 reg_dump_area = 0;
	u32 reg_dump_values[REG_DUMP_COUNT_QCA988X] = {};
	u32 host_addr;
	int ret;
	u32 i;

	ath10k_err("firmware crashed!\n");
	ath10k_err("hardware name %s version 0x%x\n",
		   ar->hw_params.name, ar->target_version);
	ath10k_err("firmware version: %s\n", ar->hw->wiphy->fw_version);

	host_addr = host_interest_item_address(HI_ITEM(hi_failure_state));
	ret = ath10k_pci_diag_read_mem(ar, host_addr,
				       &reg_dump_area, sizeof(u32));
	if (ret) {
		ath10k_err("failed to read FW dump area address: %d\n", ret);
		return;
	}

	ath10k_err("target register Dump Location: 0x%08X\n", reg_dump_area);

	ret = ath10k_pci_diag_read_mem(ar, reg_dump_area,
				       &reg_dump_values[0],
				       REG_DUMP_COUNT_QCA988X * sizeof(u32));
	if (ret != 0) {
		ath10k_err("failed to read FW dump area: %d\n", ret);
		return;
	}

	BUILD_BUG_ON(REG_DUMP_COUNT_QCA988X % 4);

	ath10k_err("target Register Dump\n");
	for (i = 0; i < REG_DUMP_COUNT_QCA988X; i += 4)
		ath10k_err("[%02d]: 0x%08X 0x%08X 0x%08X 0x%08X\n",
			   i,
			   reg_dump_values[i],
			   reg_dump_values[i + 1],
			   reg_dump_values[i + 2],
			   reg_dump_values[i + 3]);

	queue_work(ar->workqueue, &ar->restart_work);
}

static void ath10k_pci_hif_send_complete_check(struct ath10k *ar, u8 pipe,
					       int force)
{
	ath10k_dbg(ATH10K_DBG_PCI, "pci hif send complete check\n");

	if (!force) {
		int resources;
		/*
		 * Decide whether to actually poll for completions, or just
		 * wait for a later chance.
		 * If there seem to be plenty of resources left, then just wait
		 * since checking involves reading a CE register, which is a
		 * relatively expensive operation.
		 */
		resources = ath10k_pci_hif_get_free_queue_number(ar, pipe);

		/*
		 * If at least 50% of the total resources are still available,
		 * don't bother checking again yet.
		 */
		if (resources > (host_ce_config_wlan[pipe].src_nentries >> 1))
			return;
	}
	ath10k_ce_per_engine_service(ar, pipe);
}

static void ath10k_pci_hif_set_callbacks(struct ath10k *ar,
					 struct ath10k_hif_cb *callbacks)
{
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);

	ath10k_dbg(ATH10K_DBG_PCI, "pci hif set callbacks\n");

	memcpy(&ar_pci->msg_callbacks_current, callbacks,
	       sizeof(ar_pci->msg_callbacks_current));
}

static int ath10k_pci_setup_ce_irq(struct ath10k *ar)
{
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
	const struct ce_attr *attr;
	struct ath10k_pci_pipe *pipe_info;
	int pipe_num, disable_interrupts;

	for (pipe_num = 0; pipe_num < CE_COUNT; pipe_num++) {
		pipe_info = &ar_pci->pipe_info[pipe_num];

		/* Handle Diagnostic CE specially */
		if (pipe_info->ce_hdl == ar_pci->ce_diag)
			continue;

		attr = &host_ce_config_wlan[pipe_num];

		if (attr->src_nentries) {
			disable_interrupts = attr->flags & CE_ATTR_DIS_INTR;
			ath10k_ce_send_cb_register(pipe_info->ce_hdl,
						   ath10k_pci_ce_send_done,
						   disable_interrupts);
		}

		if (attr->dest_nentries)
			ath10k_ce_recv_cb_register(pipe_info->ce_hdl,
						   ath10k_pci_ce_recv_data);
	}

	return 0;
}

static void ath10k_pci_kill_tasklet(struct ath10k *ar)
{
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
	int i;

	tasklet_kill(&ar_pci->intr_tq);
	tasklet_kill(&ar_pci->msi_fw_err);
	tasklet_kill(&ar_pci->early_irq_tasklet);

	for (i = 0; i < CE_COUNT; i++)
		tasklet_kill(&ar_pci->pipe_info[i].intr);
}

/* TODO - temporary mapping while we have too few CE's */
static int ath10k_pci_hif_map_service_to_pipe(struct ath10k *ar,
					      u16 service_id, u8 *ul_pipe,
					      u8 *dl_pipe, int *ul_is_polled,
					      int *dl_is_polled)
{
	int ret = 0;

	ath10k_dbg(ATH10K_DBG_PCI, "pci hif map service\n");

	/* polling for received messages not supported */
	*dl_is_polled = 0;

	switch (service_id) {
	case ATH10K_HTC_SVC_ID_HTT_DATA_MSG:
		/*
		 * Host->target HTT gets its own pipe, so it can be polled
		 * while other pipes are interrupt driven.
		 */
		*ul_pipe = 4;
		/*
		 * Use the same target->host pipe for HTC ctrl, HTC raw
		 * streams, and HTT.
		 */
		*dl_pipe = 1;
		break;

	case ATH10K_HTC_SVC_ID_RSVD_CTRL:
	case ATH10K_HTC_SVC_ID_TEST_RAW_STREAMS:
		/*
		 * Note: HTC_RAW_STREAMS_SVC is currently unused, and
		 * HTC_CTRL_RSVD_SVC could share the same pipe as the
		 * WMI services.  So, if another CE is needed, change
		 * this to *ul_pipe = 3, which frees up CE 0.
		 */
		/* *ul_pipe = 3; */
		*ul_pipe = 0;
		*dl_pipe = 1;
		break;

	case ATH10K_HTC_SVC_ID_WMI_DATA_BK:
	case ATH10K_HTC_SVC_ID_WMI_DATA_BE:
	case ATH10K_HTC_SVC_ID_WMI_DATA_VI:
	case ATH10K_HTC_SVC_ID_WMI_DATA_VO:

	case ATH10K_HTC_SVC_ID_WMI_CONTROL:
		*ul_pipe = 3;
		*dl_pipe = 2;
		break;

		/* pipe 5 unused   */
		/* pipe 6 reserved */
		/* pipe 7 reserved */

	default:
		ret = -1;
		break;
	}
	*ul_is_polled =
		(host_ce_config_wlan[*ul_pipe].flags & CE_ATTR_DIS_INTR) != 0;

	return ret;
}

static void ath10k_pci_hif_get_default_pipe(struct ath10k *ar,
						u8 *ul_pipe, u8 *dl_pipe)
{
	int ul_is_polled, dl_is_polled;

	ath10k_dbg(ATH10K_DBG_PCI, "pci hif get default pipe\n");

	(void)ath10k_pci_hif_map_service_to_pipe(ar,
						 ATH10K_HTC_SVC_ID_RSVD_CTRL,
						 ul_pipe,
						 dl_pipe,
						 &ul_is_polled,
						 &dl_is_polled);
}

static int ath10k_pci_post_rx_pipe(struct ath10k_pci_pipe *pipe_info,
				   int num)
{
	struct ath10k *ar = pipe_info->hif_ce_state;
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
	struct ath10k_ce_pipe *ce_state = pipe_info->ce_hdl;
	struct sk_buff *skb;
	dma_addr_t ce_data;
	int i, ret = 0;

	if (pipe_info->buf_sz == 0)
		return 0;

	for (i = 0; i < num; i++) {
		skb = dev_alloc_skb(pipe_info->buf_sz);
		if (!skb) {
			ath10k_warn("failed to allocate skbuff for pipe %d\n",
				    num);
			ret = -ENOMEM;
			goto err;
		}

		WARN_ONCE((unsigned long)skb->data & 3, "unaligned skb");

		ce_data = dma_map_single(ar->dev, skb->data,
					 skb->len + skb_tailroom(skb),
					 DMA_FROM_DEVICE);

		if (unlikely(dma_mapping_error(ar->dev, ce_data))) {
			ath10k_warn("failed to DMA map sk_buff\n");
			dev_kfree_skb_any(skb);
			ret = -EIO;
			goto err;
		}

		ATH10K_SKB_CB(skb)->paddr = ce_data;

		pci_dma_sync_single_for_device(ar_pci->pdev, ce_data,
					       pipe_info->buf_sz,
					       PCI_DMA_FROMDEVICE);

		ret = ath10k_ce_recv_buf_enqueue(ce_state, (void *)skb,
						 ce_data);
		if (ret) {
			ath10k_warn("failed to enqueue to pipe %d: %d\n",
				    num, ret);
			goto err;
		}
	}

	return ret;

err:
	ath10k_pci_rx_pipe_cleanup(pipe_info);
	return ret;
}

static int ath10k_pci_post_rx(struct ath10k *ar)
{
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
	struct ath10k_pci_pipe *pipe_info;
	const struct ce_attr *attr;
	int pipe_num, ret = 0;

	for (pipe_num = 0; pipe_num < CE_COUNT; pipe_num++) {
		pipe_info = &ar_pci->pipe_info[pipe_num];
		attr = &host_ce_config_wlan[pipe_num];

		if (attr->dest_nentries == 0)
			continue;

		ret = ath10k_pci_post_rx_pipe(pipe_info,
					      attr->dest_nentries - 1);
		if (ret) {
			ath10k_warn("failed to post RX buffer for pipe %d: %d\n",
				    pipe_num, ret);

			for (; pipe_num >= 0; pipe_num--) {
				pipe_info = &ar_pci->pipe_info[pipe_num];
				ath10k_pci_rx_pipe_cleanup(pipe_info);
			}
			return ret;
		}
	}

	return 0;
}

static int ath10k_pci_hif_start(struct ath10k *ar)
{
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
	int ret, ret_early;

	ath10k_dbg(ATH10K_DBG_BOOT, "boot hif start\n");

	ath10k_pci_free_early_irq(ar);
	ath10k_pci_kill_tasklet(ar);

	ret = ath10k_pci_request_irq(ar);
	if (ret) {
		ath10k_warn("failed to post RX buffers for all pipes: %d\n",
			    ret);
		goto err_early_irq;
	}

	ret = ath10k_pci_setup_ce_irq(ar);
	if (ret) {
		ath10k_warn("failed to setup CE interrupts: %d\n", ret);
		goto err_stop;
	}

	/* Post buffers once to start things off. */
	ret = ath10k_pci_post_rx(ar);
	if (ret) {
		ath10k_warn("failed to post RX buffers for all pipes: %d\n",
			    ret);
		goto err_stop;
	}

	ar_pci->started = 1;
	return 0;

err_stop:
	ath10k_ce_disable_interrupts(ar);
	ath10k_pci_free_irq(ar);
	ath10k_pci_kill_tasklet(ar);
err_early_irq:
	/* Though there should be no interrupts (device was reset)
	 * power_down() expects the early IRQ to be installed as per the
	 * driver lifecycle. */
	ret_early = ath10k_pci_request_early_irq(ar);
	if (ret_early)
		ath10k_warn("failed to re-enable early irq: %d\n", ret_early);

	return ret;
}

static void ath10k_pci_rx_pipe_cleanup(struct ath10k_pci_pipe *pipe_info)
{
	struct ath10k *ar;
	struct ath10k_pci *ar_pci;
	struct ath10k_ce_pipe *ce_hdl;
	u32 buf_sz;
	struct sk_buff *netbuf;
	u32 ce_data;

	buf_sz = pipe_info->buf_sz;

	/* Unused Copy Engine */
	if (buf_sz == 0)
		return;

	ar = pipe_info->hif_ce_state;
	ar_pci = ath10k_pci_priv(ar);

	if (!ar_pci->started)
		return;

	ce_hdl = pipe_info->ce_hdl;

	while (ath10k_ce_revoke_recv_next(ce_hdl, (void **)&netbuf,
					  &ce_data) == 0) {
		dma_unmap_single(ar->dev, ATH10K_SKB_CB(netbuf)->paddr,
				 netbuf->len + skb_tailroom(netbuf),
				 DMA_FROM_DEVICE);
		dev_kfree_skb_any(netbuf);
	}
}

static void ath10k_pci_tx_pipe_cleanup(struct ath10k_pci_pipe *pipe_info)
{
	struct ath10k *ar;
	struct ath10k_pci *ar_pci;
	struct ath10k_ce_pipe *ce_hdl;
	struct sk_buff *netbuf;
	u32 ce_data;
	unsigned int nbytes;
	unsigned int id;
	u32 buf_sz;

	buf_sz = pipe_info->buf_sz;

	/* Unused Copy Engine */
	if (buf_sz == 0)
		return;

	ar = pipe_info->hif_ce_state;
	ar_pci = ath10k_pci_priv(ar);

	if (!ar_pci->started)
		return;

	ce_hdl = pipe_info->ce_hdl;

	while (ath10k_ce_cancel_send_next(ce_hdl, (void **)&netbuf,
					  &ce_data, &nbytes, &id) == 0) {
		/* no need to call tx completion for NULL pointers */
		if (!netbuf)
			continue;

		ar_pci->msg_callbacks_current.tx_completion(ar,
							    netbuf,
							    id);
	}
}

/*
 * Cleanup residual buffers for device shutdown:
 *    buffers that were enqueued for receive
 *    buffers that were to be sent
 * Note: Buffers that had completed but which were
 * not yet processed are on a completion queue. They
 * are handled when the completion thread shuts down.
 */
static void ath10k_pci_buffer_cleanup(struct ath10k *ar)
{
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
	int pipe_num;

	for (pipe_num = 0; pipe_num < CE_COUNT; pipe_num++) {
		struct ath10k_pci_pipe *pipe_info;

		pipe_info = &ar_pci->pipe_info[pipe_num];
		ath10k_pci_rx_pipe_cleanup(pipe_info);
		ath10k_pci_tx_pipe_cleanup(pipe_info);
	}
}

static void ath10k_pci_ce_deinit(struct ath10k *ar)
{
	int i;

	for (i = 0; i < CE_COUNT; i++)
		ath10k_ce_deinit_pipe(ar, i);
}

static void ath10k_pci_hif_stop(struct ath10k *ar)
{
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
	int ret;

	ath10k_dbg(ATH10K_DBG_BOOT, "boot hif stop\n");

	if (WARN_ON(!ar_pci->started))
		return;

	ret = ath10k_ce_disable_interrupts(ar);
	if (ret)
		ath10k_warn("failed to disable CE interrupts: %d\n", ret);

	ath10k_pci_free_irq(ar);
	ath10k_pci_kill_tasklet(ar);

	ret = ath10k_pci_request_early_irq(ar);
	if (ret)
		ath10k_warn("failed to re-enable early irq: %d\n", ret);

	/* At this point, asynchronous threads are stopped, the target should
	 * not DMA nor interrupt. We process the leftovers and then free
	 * everything else up. */

	ath10k_pci_buffer_cleanup(ar);

	/* Make the sure the device won't access any structures on the host by
	 * resetting it. The device was fed with PCI CE ringbuffer
	 * configuration during init. If ringbuffers are freed and the device
	 * were to access them this could lead to memory corruption on the
	 * host. */
	ath10k_pci_warm_reset(ar);

	ar_pci->started = 0;
}

static int ath10k_pci_hif_exchange_bmi_msg(struct ath10k *ar,
					   void *req, u32 req_len,
					   void *resp, u32 *resp_len)
{
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
	struct ath10k_pci_pipe *pci_tx = &ar_pci->pipe_info[BMI_CE_NUM_TO_TARG];
	struct ath10k_pci_pipe *pci_rx = &ar_pci->pipe_info[BMI_CE_NUM_TO_HOST];
	struct ath10k_ce_pipe *ce_tx = pci_tx->ce_hdl;
	struct ath10k_ce_pipe *ce_rx = pci_rx->ce_hdl;
	dma_addr_t req_paddr = 0;
	dma_addr_t resp_paddr = 0;
	struct bmi_xfer xfer = {};
	void *treq, *tresp = NULL;
	int ret = 0;

	might_sleep();

	if (resp && !resp_len)
		return -EINVAL;

	if (resp && resp_len && *resp_len == 0)
		return -EINVAL;

	treq = kmemdup(req, req_len, GFP_KERNEL);
	if (!treq)
		return -ENOMEM;

	req_paddr = dma_map_single(ar->dev, treq, req_len, DMA_TO_DEVICE);
	ret = dma_mapping_error(ar->dev, req_paddr);
	if (ret)
		goto err_dma;

	if (resp && resp_len) {
		tresp = kzalloc(*resp_len, GFP_KERNEL);
		if (!tresp) {
			ret = -ENOMEM;
			goto err_req;
		}

		resp_paddr = dma_map_single(ar->dev, tresp, *resp_len,
					    DMA_FROM_DEVICE);
		ret = dma_mapping_error(ar->dev, resp_paddr);
		if (ret)
			goto err_req;

		xfer.wait_for_resp = true;
		xfer.resp_len = 0;

		ath10k_ce_recv_buf_enqueue(ce_rx, &xfer, resp_paddr);
	}

	ret = ath10k_ce_send(ce_tx, &xfer, req_paddr, req_len, -1, 0);
	if (ret)
		goto err_resp;

	ret = ath10k_pci_bmi_wait(ce_tx, ce_rx, &xfer);
	if (ret) {
		u32 unused_buffer;
		unsigned int unused_nbytes;
		unsigned int unused_id;

		ath10k_ce_cancel_send_next(ce_tx, NULL, &unused_buffer,
					   &unused_nbytes, &unused_id);
	} else {
		/* non-zero means we did not time out */
		ret = 0;
	}

err_resp:
	if (resp) {
		u32 unused_buffer;

		ath10k_ce_revoke_recv_next(ce_rx, NULL, &unused_buffer);
		dma_unmap_single(ar->dev, resp_paddr,
				 *resp_len, DMA_FROM_DEVICE);
	}
err_req:
	dma_unmap_single(ar->dev, req_paddr, req_len, DMA_TO_DEVICE);

	if (ret == 0 && resp_len) {
		*resp_len = min(*resp_len, xfer.resp_len);
		memcpy(resp, tresp, xfer.resp_len);
	}
err_dma:
	kfree(treq);
	kfree(tresp);

	return ret;
}

static void ath10k_pci_bmi_send_done(struct ath10k_ce_pipe *ce_state)
{
	struct bmi_xfer *xfer;
	u32 ce_data;
	unsigned int nbytes;
	unsigned int transfer_id;

	if (ath10k_ce_completed_send_next(ce_state, (void **)&xfer, &ce_data,
					  &nbytes, &transfer_id))
		return;

	xfer->tx_done = true;
}

static void ath10k_pci_bmi_recv_data(struct ath10k_ce_pipe *ce_state)
{
	struct bmi_xfer *xfer;
	u32 ce_data;
	unsigned int nbytes;
	unsigned int transfer_id;
	unsigned int flags;

	if (ath10k_ce_completed_recv_next(ce_state, (void **)&xfer, &ce_data,
					  &nbytes, &transfer_id, &flags))
		return;

	if (!xfer->wait_for_resp) {
		ath10k_warn("unexpected: BMI data received; ignoring\n");
		return;
	}

	xfer->resp_len = nbytes;
	xfer->rx_done = true;
}

static int ath10k_pci_bmi_wait(struct ath10k_ce_pipe *tx_pipe,
			       struct ath10k_ce_pipe *rx_pipe,
			       struct bmi_xfer *xfer)
{
	unsigned long timeout = jiffies + BMI_COMMUNICATION_TIMEOUT_HZ;

	while (time_before_eq(jiffies, timeout)) {
		ath10k_pci_bmi_send_done(tx_pipe);
		ath10k_pci_bmi_recv_data(rx_pipe);

		if (xfer->tx_done && (xfer->rx_done == xfer->wait_for_resp))
			return 0;

		schedule();
	}

	return -ETIMEDOUT;
}

/*
 * Map from service/endpoint to Copy Engine.
 * This table is derived from the CE_PCI TABLE, above.
 * It is passed to the Target at startup for use by firmware.
 */
static const struct service_to_pipe target_service_to_ce_map_wlan[] = {
	{
		 ATH10K_HTC_SVC_ID_WMI_DATA_VO,
		 PIPEDIR_OUT,		/* out = UL = host -> target */
		 3,
	},
	{
		 ATH10K_HTC_SVC_ID_WMI_DATA_VO,
		 PIPEDIR_IN,		/* in = DL = target -> host */
		 2,
	},
	{
		 ATH10K_HTC_SVC_ID_WMI_DATA_BK,
		 PIPEDIR_OUT,		/* out = UL = host -> target */
		 3,
	},
	{
		 ATH10K_HTC_SVC_ID_WMI_DATA_BK,
		 PIPEDIR_IN,		/* in = DL = target -> host */
		 2,
	},
	{
		 ATH10K_HTC_SVC_ID_WMI_DATA_BE,
		 PIPEDIR_OUT,		/* out = UL = host -> target */
		 3,
	},
	{
		 ATH10K_HTC_SVC_ID_WMI_DATA_BE,
		 PIPEDIR_IN,		/* in = DL = target -> host */
		 2,
	},
	{
		 ATH10K_HTC_SVC_ID_WMI_DATA_VI,
		 PIPEDIR_OUT,		/* out = UL = host -> target */
		 3,
	},
	{
		 ATH10K_HTC_SVC_ID_WMI_DATA_VI,
		 PIPEDIR_IN,		/* in = DL = target -> host */
		 2,
	},
	{
		 ATH10K_HTC_SVC_ID_WMI_CONTROL,
		 PIPEDIR_OUT,		/* out = UL = host -> target */
		 3,
	},
	{
		 ATH10K_HTC_SVC_ID_WMI_CONTROL,
		 PIPEDIR_IN,		/* in = DL = target -> host */
		 2,
	},
	{
		 ATH10K_HTC_SVC_ID_RSVD_CTRL,
		 PIPEDIR_OUT,		/* out = UL = host -> target */
		 0,		/* could be moved to 3 (share with WMI) */
	},
	{
		 ATH10K_HTC_SVC_ID_RSVD_CTRL,
		 PIPEDIR_IN,		/* in = DL = target -> host */
		 1,
	},
	{
		 ATH10K_HTC_SVC_ID_TEST_RAW_STREAMS,	/* not currently used */
		 PIPEDIR_OUT,		/* out = UL = host -> target */
		 0,
	},
	{
		 ATH10K_HTC_SVC_ID_TEST_RAW_STREAMS,	/* not currently used */
		 PIPEDIR_IN,		/* in = DL = target -> host */
		 1,
	},
	{
		 ATH10K_HTC_SVC_ID_HTT_DATA_MSG,
		 PIPEDIR_OUT,		/* out = UL = host -> target */
		 4,
	},
	{
		 ATH10K_HTC_SVC_ID_HTT_DATA_MSG,
		 PIPEDIR_IN,		/* in = DL = target -> host */
		 1,
	},

	/* (Additions here) */

	{				/* Must be last */
		 0,
		 0,
		 0,
	},
};

/*
 * Send an interrupt to the device to wake up the Target CPU
 * so it has an opportunity to notice any changed state.
 */
static int ath10k_pci_wake_target_cpu(struct ath10k *ar)
{
	int ret;
	u32 core_ctrl;

	ret = ath10k_pci_diag_read_access(ar, SOC_CORE_BASE_ADDRESS |
					      CORE_CTRL_ADDRESS,
					  &core_ctrl);
	if (ret) {
		ath10k_warn("failed to read core_ctrl: %d\n", ret);
		return ret;
	}

	/* A_INUM_FIRMWARE interrupt to Target CPU */
	core_ctrl |= CORE_CTRL_CPU_INTR_MASK;

	ret = ath10k_pci_diag_write_access(ar, SOC_CORE_BASE_ADDRESS |
					       CORE_CTRL_ADDRESS,
					   core_ctrl);
	if (ret) {
		ath10k_warn("failed to set target CPU interrupt mask: %d\n",
			    ret);
		return ret;
	}

	return 0;
}

static int ath10k_pci_init_config(struct ath10k *ar)
{
	u32 interconnect_targ_addr;
	u32 pcie_state_targ_addr = 0;
	u32 pipe_cfg_targ_addr = 0;
	u32 svc_to_pipe_map = 0;
	u32 pcie_config_flags = 0;
	u32 ealloc_value;
	u32 ealloc_targ_addr;
	u32 flag2_value;
	u32 flag2_targ_addr;
	int ret = 0;

	/* Download to Target the CE Config and the service-to-CE map */
	interconnect_targ_addr =
		host_interest_item_address(HI_ITEM(hi_interconnect_state));

	/* Supply Target-side CE configuration */
	ret = ath10k_pci_diag_read_access(ar, interconnect_targ_addr,
					  &pcie_state_targ_addr);
	if (ret != 0) {
		ath10k_err("Failed to get pcie state addr: %d\n", ret);
		return ret;
	}

	if (pcie_state_targ_addr == 0) {
		ret = -EIO;
		ath10k_err("Invalid pcie state addr\n");
		return ret;
	}

	ret = ath10k_pci_diag_read_access(ar, pcie_state_targ_addr +
					  offsetof(struct pcie_state,
						   pipe_cfg_addr),
					  &pipe_cfg_targ_addr);
	if (ret != 0) {
		ath10k_err("Failed to get pipe cfg addr: %d\n", ret);
		return ret;
	}

	if (pipe_cfg_targ_addr == 0) {
		ret = -EIO;
		ath10k_err("Invalid pipe cfg addr\n");
		return ret;
	}

	ret = ath10k_pci_diag_write_mem(ar, pipe_cfg_targ_addr,
				 target_ce_config_wlan,
				 sizeof(target_ce_config_wlan));

	if (ret != 0) {
		ath10k_err("Failed to write pipe cfg: %d\n", ret);
		return ret;
	}

	ret = ath10k_pci_diag_read_access(ar, pcie_state_targ_addr +
					  offsetof(struct pcie_state,
						   svc_to_pipe_map),
					  &svc_to_pipe_map);
	if (ret != 0) {
		ath10k_err("Failed to get svc/pipe map: %d\n", ret);
		return ret;
	}

	if (svc_to_pipe_map == 0) {
		ret = -EIO;
		ath10k_err("Invalid svc_to_pipe map\n");
		return ret;
	}

	ret = ath10k_pci_diag_write_mem(ar, svc_to_pipe_map,
				 target_service_to_ce_map_wlan,
				 sizeof(target_service_to_ce_map_wlan));
	if (ret != 0) {
		ath10k_err("Failed to write svc/pipe map: %d\n", ret);
		return ret;
	}

	ret = ath10k_pci_diag_read_access(ar, pcie_state_targ_addr +
					  offsetof(struct pcie_state,
						   config_flags),
					  &pcie_config_flags);
	if (ret != 0) {
		ath10k_err("Failed to get pcie config_flags: %d\n", ret);
		return ret;
	}

	pcie_config_flags &= ~PCIE_CONFIG_FLAG_ENABLE_L1;

	ret = ath10k_pci_diag_write_mem(ar, pcie_state_targ_addr +
				 offsetof(struct pcie_state, config_flags),
				 &pcie_config_flags,
				 sizeof(pcie_config_flags));
	if (ret != 0) {
		ath10k_err("Failed to write pcie config_flags: %d\n", ret);
		return ret;
	}

	/* configure early allocation */
	ealloc_targ_addr = host_interest_item_address(HI_ITEM(hi_early_alloc));

	ret = ath10k_pci_diag_read_access(ar, ealloc_targ_addr, &ealloc_value);
	if (ret != 0) {
		ath10k_err("Faile to get early alloc val: %d\n", ret);
		return ret;
	}

	/* first bank is switched to IRAM */
	ealloc_value |= ((HI_EARLY_ALLOC_MAGIC << HI_EARLY_ALLOC_MAGIC_SHIFT) &
			 HI_EARLY_ALLOC_MAGIC_MASK);
	ealloc_value |= ((1 << HI_EARLY_ALLOC_IRAM_BANKS_SHIFT) &
			 HI_EARLY_ALLOC_IRAM_BANKS_MASK);

	ret = ath10k_pci_diag_write_access(ar, ealloc_targ_addr, ealloc_value);
	if (ret != 0) {
		ath10k_err("Failed to set early alloc val: %d\n", ret);
		return ret;
	}

	/* Tell Target to proceed with initialization */
	flag2_targ_addr = host_interest_item_address(HI_ITEM(hi_option_flag2));

	ret = ath10k_pci_diag_read_access(ar, flag2_targ_addr, &flag2_value);
	if (ret != 0) {
		ath10k_err("Failed to get option val: %d\n", ret);
		return ret;
	}

	flag2_value |= HI_OPTION_EARLY_CFG_DONE;

	ret = ath10k_pci_diag_write_access(ar, flag2_targ_addr, flag2_value);
	if (ret != 0) {
		ath10k_err("Failed to set option val: %d\n", ret);
		return ret;
	}

	return 0;
}

static int ath10k_pci_alloc_ce(struct ath10k *ar)
{
	int i, ret;

	for (i = 0; i < CE_COUNT; i++) {
		ret = ath10k_ce_alloc_pipe(ar, i, &host_ce_config_wlan[i]);
		if (ret) {
			ath10k_err("failed to allocate copy engine pipe %d: %d\n",
				   i, ret);
			return ret;
		}
	}

	return 0;
}

static void ath10k_pci_free_ce(struct ath10k *ar)
{
	int i;

	for (i = 0; i < CE_COUNT; i++)
		ath10k_ce_free_pipe(ar, i);
}

static int ath10k_pci_ce_init(struct ath10k *ar)
{
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
	struct ath10k_pci_pipe *pipe_info;
	const struct ce_attr *attr;
	int pipe_num, ret;

	for (pipe_num = 0; pipe_num < CE_COUNT; pipe_num++) {
		pipe_info = &ar_pci->pipe_info[pipe_num];
		pipe_info->ce_hdl = &ar_pci->ce_states[pipe_num];
		pipe_info->pipe_num = pipe_num;
		pipe_info->hif_ce_state = ar;
		attr = &host_ce_config_wlan[pipe_num];

		ret = ath10k_ce_init_pipe(ar, pipe_num, attr);
		if (ret) {
			ath10k_err("failed to initialize copy engine pipe %d: %d\n",
				   pipe_num, ret);
			return ret;
		}

		if (pipe_num == CE_COUNT - 1) {
			/*
			 * Reserve the ultimate CE for
			 * diagnostic Window support
			 */
			ar_pci->ce_diag = pipe_info->ce_hdl;
			continue;
		}

		pipe_info->buf_sz = (size_t) (attr->src_sz_max);
	}

	return 0;
}

static void ath10k_pci_fw_interrupt_handler(struct ath10k *ar)
{
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
	u32 fw_indicator;

	fw_indicator = ath10k_pci_read32(ar, FW_INDICATOR_ADDRESS);

	if (fw_indicator & FW_IND_EVENT_PENDING) {
		/* ACK: clear Target-side pending event */
		ath10k_pci_write32(ar, FW_INDICATOR_ADDRESS,
				   fw_indicator & ~FW_IND_EVENT_PENDING);

		if (ar_pci->started) {
			ath10k_pci_hif_dump_area(ar);
		} else {
			/*
			 * Probable Target failure before we're prepared
			 * to handle it.  Generally unexpected.
			 */
			ath10k_warn("early firmware event indicated\n");
		}
	}
}

/* this function effectively clears target memory controller assert line */
static void ath10k_pci_warm_reset_si0(struct ath10k *ar)
{
	u32 val;

	val = ath10k_pci_soc_read32(ar, SOC_RESET_CONTROL_ADDRESS);
	ath10k_pci_soc_write32(ar, SOC_RESET_CONTROL_ADDRESS,
			       val | SOC_RESET_CONTROL_SI0_RST_MASK);
	val = ath10k_pci_soc_read32(ar, SOC_RESET_CONTROL_ADDRESS);

	msleep(10);

	val = ath10k_pci_soc_read32(ar, SOC_RESET_CONTROL_ADDRESS);
	ath10k_pci_soc_write32(ar, SOC_RESET_CONTROL_ADDRESS,
			       val & ~SOC_RESET_CONTROL_SI0_RST_MASK);
	val = ath10k_pci_soc_read32(ar, SOC_RESET_CONTROL_ADDRESS);

	msleep(10);
}

static int ath10k_pci_warm_reset(struct ath10k *ar)
{
	u32 val;

	ath10k_dbg(ATH10K_DBG_BOOT, "boot warm reset\n");

	/* debug */
	val = ath10k_pci_read32(ar, SOC_CORE_BASE_ADDRESS +
				PCIE_INTR_CAUSE_ADDRESS);
	ath10k_dbg(ATH10K_DBG_BOOT, "boot host cpu intr cause: 0x%08x\n", val);

	val = ath10k_pci_read32(ar, SOC_CORE_BASE_ADDRESS +
				CPU_INTR_ADDRESS);
	ath10k_dbg(ATH10K_DBG_BOOT, "boot target cpu intr cause: 0x%08x\n",
		   val);

	/* disable pending irqs */
	ath10k_pci_write32(ar, SOC_CORE_BASE_ADDRESS +
			   PCIE_INTR_ENABLE_ADDRESS, 0);

	ath10k_pci_write32(ar, SOC_CORE_BASE_ADDRESS +
			   PCIE_INTR_CLR_ADDRESS, ~0);

	msleep(100);

	/* clear fw indicator */
	ath10k_pci_write32(ar, FW_INDICATOR_ADDRESS, 0);

	/* clear target LF timer interrupts */
	val = ath10k_pci_read32(ar, RTC_SOC_BASE_ADDRESS +
				SOC_LF_TIMER_CONTROL0_ADDRESS);
	ath10k_pci_write32(ar, RTC_SOC_BASE_ADDRESS +
			   SOC_LF_TIMER_CONTROL0_ADDRESS,
			   val & ~SOC_LF_TIMER_CONTROL0_ENABLE_MASK);

	/* reset CE */
	val = ath10k_pci_read32(ar, RTC_SOC_BASE_ADDRESS +
				SOC_RESET_CONTROL_ADDRESS);
	ath10k_pci_write32(ar, RTC_SOC_BASE_ADDRESS + SOC_RESET_CONTROL_ADDRESS,
			   val | SOC_RESET_CONTROL_CE_RST_MASK);
	val = ath10k_pci_read32(ar, RTC_SOC_BASE_ADDRESS +
				SOC_RESET_CONTROL_ADDRESS);
	msleep(10);

	/* unreset CE */
	ath10k_pci_write32(ar, RTC_SOC_BASE_ADDRESS + SOC_RESET_CONTROL_ADDRESS,
			   val & ~SOC_RESET_CONTROL_CE_RST_MASK);
	val = ath10k_pci_read32(ar, RTC_SOC_BASE_ADDRESS +
				SOC_RESET_CONTROL_ADDRESS);
	msleep(10);

	ath10k_pci_warm_reset_si0(ar);

	/* debug */
	val = ath10k_pci_read32(ar, SOC_CORE_BASE_ADDRESS +
				PCIE_INTR_CAUSE_ADDRESS);
	ath10k_dbg(ATH10K_DBG_BOOT, "boot host cpu intr cause: 0x%08x\n", val);

	val = ath10k_pci_read32(ar, SOC_CORE_BASE_ADDRESS +
				CPU_INTR_ADDRESS);
	ath10k_dbg(ATH10K_DBG_BOOT, "boot target cpu intr cause: 0x%08x\n",
		   val);

	/* CPU warm reset */
	val = ath10k_pci_read32(ar, RTC_SOC_BASE_ADDRESS +
				SOC_RESET_CONTROL_ADDRESS);
	ath10k_pci_write32(ar, RTC_SOC_BASE_ADDRESS + SOC_RESET_CONTROL_ADDRESS,
			   val | SOC_RESET_CONTROL_CPU_WARM_RST_MASK);

	val = ath10k_pci_read32(ar, RTC_SOC_BASE_ADDRESS +
				SOC_RESET_CONTROL_ADDRESS);
	ath10k_dbg(ATH10K_DBG_BOOT, "boot target reset state: 0x%08x\n", val);

	msleep(100);

	ath10k_dbg(ATH10K_DBG_BOOT, "boot warm reset complete\n");

	return 0;
}

static int __ath10k_pci_hif_power_up(struct ath10k *ar, bool cold_reset)
{
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
	const char *irq_mode;
	int ret;

	/*
	 * Bring the target up cleanly.
	 *
	 * The target may be in an undefined state with an AUX-powered Target
	 * and a Host in WoW mode. If the Host crashes, loses power, or is
	 * restarted (without unloading the driver) then the Target is left
	 * (aux) powered and running. On a subsequent driver load, the Target
	 * is in an unexpected state. We try to catch that here in order to
	 * reset the Target and retry the probe.
	 */
	if (cold_reset)
		ret = ath10k_pci_cold_reset(ar);
	else
		ret = ath10k_pci_warm_reset(ar);

	if (ret) {
		ath10k_err("failed to reset target: %d\n", ret);
		goto err;
	}

	ret = ath10k_pci_ce_init(ar);
	if (ret) {
		ath10k_err("failed to initialize CE: %d\n", ret);
		goto err;
	}

	ret = ath10k_ce_disable_interrupts(ar);
	if (ret) {
		ath10k_err("failed to disable CE interrupts: %d\n", ret);
		goto err_ce;
	}

	ret = ath10k_pci_init_irq(ar);
	if (ret) {
		ath10k_err("failed to init irqs: %d\n", ret);
		goto err_ce;
	}

	ret = ath10k_pci_request_early_irq(ar);
	if (ret) {
		ath10k_err("failed to request early irq: %d\n", ret);
		goto err_deinit_irq;
	}

	ret = ath10k_pci_wait_for_target_init(ar);
	if (ret) {
		ath10k_err("failed to wait for target to init: %d\n", ret);
		goto err_free_early_irq;
	}

	ret = ath10k_pci_init_config(ar);
	if (ret) {
		ath10k_err("failed to setup init config: %d\n", ret);
		goto err_free_early_irq;
	}

	ret = ath10k_pci_wake_target_cpu(ar);
	if (ret) {
		ath10k_err("could not wake up target CPU: %d\n", ret);
		goto err_free_early_irq;
	}

	if (ar_pci->num_msi_intrs > 1)
		irq_mode = "MSI-X";
	else if (ar_pci->num_msi_intrs == 1)
		irq_mode = "MSI";
	else
		irq_mode = "legacy";

	if (!test_bit(ATH10K_FLAG_FIRST_BOOT_DONE, &ar->dev_flags))
		ath10k_info("pci irq %s irq_mode %d reset_mode %d\n",
			    irq_mode, ath10k_pci_irq_mode,
			    ath10k_pci_reset_mode);

	return 0;

err_free_early_irq:
	ath10k_pci_free_early_irq(ar);
err_deinit_irq:
	ath10k_pci_deinit_irq(ar);
err_ce:
	ath10k_pci_ce_deinit(ar);
	ath10k_pci_warm_reset(ar);
err:
	return ret;
}

static int ath10k_pci_hif_power_up_warm(struct ath10k *ar)
{
	int i, ret;

	/*
	 * Sometime warm reset succeeds after retries.
	 *
	 * FIXME: It might be possible to tune ath10k_pci_warm_reset() to work
	 * at first try.
	 */
	for (i = 0; i < ATH10K_PCI_NUM_WARM_RESET_ATTEMPTS; i++) {
		ret = __ath10k_pci_hif_power_up(ar, false);
		if (ret == 0)
			break;

		ath10k_warn("failed to warm reset (attempt %d out of %d): %d\n",
			    i + 1, ATH10K_PCI_NUM_WARM_RESET_ATTEMPTS, ret);
	}

	return ret;
}

static int ath10k_pci_hif_power_up(struct ath10k *ar)
{
	int ret;

	ath10k_dbg(ATH10K_DBG_BOOT, "boot hif power up\n");

	/*
	 * Hardware CUS232 version 2 has some issues with cold reset and the
	 * preferred (and safer) way to perform a device reset is through a
	 * warm reset.
	 *
	 * Warm reset doesn't always work though so fall back to cold reset may
	 * be necessary.
	 */
	ret = ath10k_pci_hif_power_up_warm(ar);
	if (ret) {
		ath10k_warn("failed to power up target using warm reset: %d\n",
			    ret);

		if (ath10k_pci_reset_mode == ATH10K_PCI_RESET_WARM_ONLY)
			return ret;

		ath10k_warn("trying cold reset\n");

		ret = __ath10k_pci_hif_power_up(ar, true);
		if (ret) {
			ath10k_err("failed to power up target using cold reset too (%d)\n",
				   ret);
			return ret;
		}
	}

	return 0;
}

static void ath10k_pci_hif_power_down(struct ath10k *ar)
{
	ath10k_dbg(ATH10K_DBG_BOOT, "boot hif power down\n");

	ath10k_pci_free_early_irq(ar);
	ath10k_pci_kill_tasklet(ar);
	ath10k_pci_deinit_irq(ar);
	ath10k_pci_ce_deinit(ar);
	ath10k_pci_warm_reset(ar);
}

#ifdef CONFIG_PM

#define ATH10K_PCI_PM_CONTROL 0x44

static int ath10k_pci_hif_suspend(struct ath10k *ar)
{
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
	struct pci_dev *pdev = ar_pci->pdev;
	u32 val;

	pci_read_config_dword(pdev, ATH10K_PCI_PM_CONTROL, &val);

	if ((val & 0x000000ff) != 0x3) {
		pci_save_state(pdev);
		pci_disable_device(pdev);
		pci_write_config_dword(pdev, ATH10K_PCI_PM_CONTROL,
				       (val & 0xffffff00) | 0x03);
	}

	return 0;
}

static int ath10k_pci_hif_resume(struct ath10k *ar)
{
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
	struct pci_dev *pdev = ar_pci->pdev;
	u32 val;

	pci_read_config_dword(pdev, ATH10K_PCI_PM_CONTROL, &val);

	if ((val & 0x000000ff) != 0) {
		pci_restore_state(pdev);
		pci_write_config_dword(pdev, ATH10K_PCI_PM_CONTROL,
				       val & 0xffffff00);
		/*
		 * Suspend/Resume resets the PCI configuration space,
		 * so we have to re-disable the RETRY_TIMEOUT register (0x41)
		 * to keep PCI Tx retries from interfering with C3 CPU state
		 */
		pci_read_config_dword(pdev, 0x40, &val);

		if ((val & 0x0000ff00) != 0)
			pci_write_config_dword(pdev, 0x40, val & 0xffff00ff);
	}

	return 0;
}
#endif

static const struct ath10k_hif_ops ath10k_pci_hif_ops = {
	.tx_sg			= ath10k_pci_hif_tx_sg,
	.exchange_bmi_msg	= ath10k_pci_hif_exchange_bmi_msg,
	.start			= ath10k_pci_hif_start,
	.stop			= ath10k_pci_hif_stop,
	.map_service_to_pipe	= ath10k_pci_hif_map_service_to_pipe,
	.get_default_pipe	= ath10k_pci_hif_get_default_pipe,
	.send_complete_check	= ath10k_pci_hif_send_complete_check,
	.set_callbacks		= ath10k_pci_hif_set_callbacks,
	.get_free_queue_number	= ath10k_pci_hif_get_free_queue_number,
	.power_up		= ath10k_pci_hif_power_up,
	.power_down		= ath10k_pci_hif_power_down,
#ifdef CONFIG_PM
	.suspend		= ath10k_pci_hif_suspend,
	.resume			= ath10k_pci_hif_resume,
#endif
};

static void ath10k_pci_ce_tasklet(unsigned long ptr)
{
	struct ath10k_pci_pipe *pipe = (struct ath10k_pci_pipe *)ptr;
	struct ath10k_pci *ar_pci = pipe->ar_pci;

	ath10k_ce_per_engine_service(ar_pci->ar, pipe->pipe_num);
}

static void ath10k_msi_err_tasklet(unsigned long data)
{
	struct ath10k *ar = (struct ath10k *)data;

	ath10k_pci_fw_interrupt_handler(ar);
}

/*
 * Handler for a per-engine interrupt on a PARTICULAR CE.
 * This is used in cases where each CE has a private MSI interrupt.
 */
static irqreturn_t ath10k_pci_per_engine_handler(int irq, void *arg)
{
	struct ath10k *ar = arg;
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
	int ce_id = irq - ar_pci->pdev->irq - MSI_ASSIGN_CE_INITIAL;

	if (ce_id < 0 || ce_id >= ARRAY_SIZE(ar_pci->pipe_info)) {
		ath10k_warn("unexpected/invalid irq %d ce_id %d\n", irq, ce_id);
		return IRQ_HANDLED;
	}

	/*
	 * NOTE: We are able to derive ce_id from irq because we
	 * use a one-to-one mapping for CE's 0..5.
	 * CE's 6 & 7 do not use interrupts at all.
	 *
	 * This mapping must be kept in sync with the mapping
	 * used by firmware.
	 */
	tasklet_schedule(&ar_pci->pipe_info[ce_id].intr);
	return IRQ_HANDLED;
}

static irqreturn_t ath10k_pci_msi_fw_handler(int irq, void *arg)
{
	struct ath10k *ar = arg;
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);

	tasklet_schedule(&ar_pci->msi_fw_err);
	return IRQ_HANDLED;
}

/*
 * Top-level interrupt handler for all PCI interrupts from a Target.
 * When a block of MSI interrupts is allocated, this top-level handler
 * is not used; instead, we directly call the correct sub-handler.
 */
static irqreturn_t ath10k_pci_interrupt_handler(int irq, void *arg)
{
	struct ath10k *ar = arg;
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);

	if (ar_pci->num_msi_intrs == 0) {
		if (!ath10k_pci_irq_pending(ar))
			return IRQ_NONE;

		ath10k_pci_disable_and_clear_legacy_irq(ar);
	}

	tasklet_schedule(&ar_pci->intr_tq);

	return IRQ_HANDLED;
}

static void ath10k_pci_early_irq_tasklet(unsigned long data)
{
	struct ath10k *ar = (struct ath10k *)data;
	u32 fw_ind;

	fw_ind = ath10k_pci_read32(ar, FW_INDICATOR_ADDRESS);
	if (fw_ind & FW_IND_EVENT_PENDING) {
		ath10k_pci_write32(ar, FW_INDICATOR_ADDRESS,
				   fw_ind & ~FW_IND_EVENT_PENDING);
		ath10k_pci_hif_dump_area(ar);
	}

	ath10k_pci_enable_legacy_irq(ar);
}

static void ath10k_pci_tasklet(unsigned long data)
{
	struct ath10k *ar = (struct ath10k *)data;
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);

	ath10k_pci_fw_interrupt_handler(ar); /* FIXME: Handle FW error */
	ath10k_ce_per_engine_service_any(ar);

	/* Re-enable legacy irq that was disabled in the irq handler */
	if (ar_pci->num_msi_intrs == 0)
		ath10k_pci_enable_legacy_irq(ar);
}

static int ath10k_pci_request_irq_msix(struct ath10k *ar)
{
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
	int ret, i;

	ret = request_irq(ar_pci->pdev->irq + MSI_ASSIGN_FW,
			  ath10k_pci_msi_fw_handler,
			  IRQF_SHARED, "ath10k_pci", ar);
	if (ret) {
		ath10k_warn("failed to request MSI-X fw irq %d: %d\n",
			    ar_pci->pdev->irq + MSI_ASSIGN_FW, ret);
		return ret;
	}

	for (i = MSI_ASSIGN_CE_INITIAL; i <= MSI_ASSIGN_CE_MAX; i++) {
		ret = request_irq(ar_pci->pdev->irq + i,
				  ath10k_pci_per_engine_handler,
				  IRQF_SHARED, "ath10k_pci", ar);
		if (ret) {
			ath10k_warn("failed to request MSI-X ce irq %d: %d\n",
				    ar_pci->pdev->irq + i, ret);

			for (i--; i >= MSI_ASSIGN_CE_INITIAL; i--)
				free_irq(ar_pci->pdev->irq + i, ar);

			free_irq(ar_pci->pdev->irq + MSI_ASSIGN_FW, ar);
			return ret;
		}
	}

	return 0;
}

static int ath10k_pci_request_irq_msi(struct ath10k *ar)
{
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
	int ret;

	ret = request_irq(ar_pci->pdev->irq,
			  ath10k_pci_interrupt_handler,
			  IRQF_SHARED, "ath10k_pci", ar);
	if (ret) {
		ath10k_warn("failed to request MSI irq %d: %d\n",
			    ar_pci->pdev->irq, ret);
		return ret;
	}

	return 0;
}

static int ath10k_pci_request_irq_legacy(struct ath10k *ar)
{
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
	int ret;

	ret = request_irq(ar_pci->pdev->irq,
			  ath10k_pci_interrupt_handler,
			  IRQF_SHARED, "ath10k_pci", ar);
	if (ret) {
		ath10k_warn("failed to request legacy irq %d: %d\n",
			    ar_pci->pdev->irq, ret);
		return ret;
	}

	return 0;
}

static int ath10k_pci_request_irq(struct ath10k *ar)
{
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);

	switch (ar_pci->num_msi_intrs) {
	case 0:
		return ath10k_pci_request_irq_legacy(ar);
	case 1:
		return ath10k_pci_request_irq_msi(ar);
	case MSI_NUM_REQUEST:
		return ath10k_pci_request_irq_msix(ar);
	}

	ath10k_warn("unknown irq configuration upon request\n");
	return -EINVAL;
}

static void ath10k_pci_free_irq(struct ath10k *ar)
{
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
	int i;

	/* There's at least one interrupt irregardless whether its legacy INTR
	 * or MSI or MSI-X */
	for (i = 0; i < max(1, ar_pci->num_msi_intrs); i++)
		free_irq(ar_pci->pdev->irq + i, ar);
}

static void ath10k_pci_init_irq_tasklets(struct ath10k *ar)
{
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
	int i;

	tasklet_init(&ar_pci->intr_tq, ath10k_pci_tasklet, (unsigned long)ar);
	tasklet_init(&ar_pci->msi_fw_err, ath10k_msi_err_tasklet,
		     (unsigned long)ar);
	tasklet_init(&ar_pci->early_irq_tasklet, ath10k_pci_early_irq_tasklet,
		     (unsigned long)ar);

	for (i = 0; i < CE_COUNT; i++) {
		ar_pci->pipe_info[i].ar_pci = ar_pci;
		tasklet_init(&ar_pci->pipe_info[i].intr, ath10k_pci_ce_tasklet,
			     (unsigned long)&ar_pci->pipe_info[i]);
	}
}

static int ath10k_pci_init_irq(struct ath10k *ar)
{
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
	int ret;

	ath10k_pci_init_irq_tasklets(ar);

	if (ath10k_pci_irq_mode != ATH10K_PCI_IRQ_AUTO &&
	    !test_bit(ATH10K_FLAG_FIRST_BOOT_DONE, &ar->dev_flags))
		ath10k_info("limiting irq mode to: %d\n", ath10k_pci_irq_mode);

	/* Try MSI-X */
	if (ath10k_pci_irq_mode == ATH10K_PCI_IRQ_AUTO) {
		ar_pci->num_msi_intrs = MSI_NUM_REQUEST;
		ret = pci_enable_msi_range(ar_pci->pdev, ar_pci->num_msi_intrs,
							 ar_pci->num_msi_intrs);
		if (ret > 0)
			return 0;

		/* fall-through */
	}

	/* Try MSI */
	if (ath10k_pci_irq_mode != ATH10K_PCI_IRQ_LEGACY) {
		ar_pci->num_msi_intrs = 1;
		ret = pci_enable_msi(ar_pci->pdev);
		if (ret == 0)
			return 0;

		/* fall-through */
	}

	/* Try legacy irq
	 *
	 * A potential race occurs here: The CORE_BASE write
	 * depends on target correctly decoding AXI address but
	 * host won't know when target writes BAR to CORE_CTRL.
	 * This write might get lost if target has NOT written BAR.
	 * For now, fix the race by repeating the write in below
	 * synchronization checking. */
	ar_pci->num_msi_intrs = 0;

	ath10k_pci_write32(ar, SOC_CORE_BASE_ADDRESS + PCIE_INTR_ENABLE_ADDRESS,
			   PCIE_INTR_FIRMWARE_MASK | PCIE_INTR_CE_MASK_ALL);

	return 0;
}

static void ath10k_pci_deinit_irq_legacy(struct ath10k *ar)
{
	ath10k_pci_write32(ar, SOC_CORE_BASE_ADDRESS + PCIE_INTR_ENABLE_ADDRESS,
			   0);
}

static int ath10k_pci_deinit_irq(struct ath10k *ar)
{
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);

	switch (ar_pci->num_msi_intrs) {
	case 0:
		ath10k_pci_deinit_irq_legacy(ar);
		return 0;
	case 1:
		/* fall-through */
	case MSI_NUM_REQUEST:
		pci_disable_msi(ar_pci->pdev);
		return 0;
	default:
		pci_disable_msi(ar_pci->pdev);
	}

	ath10k_warn("unknown irq configuration upon deinit\n");
	return -EINVAL;
}

static int ath10k_pci_wait_for_target_init(struct ath10k *ar)
{
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
	unsigned long timeout;
	u32 val;

	ath10k_dbg(ATH10K_DBG_BOOT, "boot waiting target to initialise\n");

	timeout = jiffies + msecs_to_jiffies(ATH10K_PCI_TARGET_WAIT);

	do {
		val = ath10k_pci_read32(ar, FW_INDICATOR_ADDRESS);

		ath10k_dbg(ATH10K_DBG_BOOT, "boot target indicator %x\n", val);

		/* target should never return this */
		if (val == 0xffffffff)
			continue;

		/* the device has crashed so don't bother trying anymore */
		if (val & FW_IND_EVENT_PENDING)
			break;

		if (val & FW_IND_INITIALIZED)
			break;

		if (ar_pci->num_msi_intrs == 0)
			/* Fix potential race by repeating CORE_BASE writes */
			ath10k_pci_soc_write32(ar, PCIE_INTR_ENABLE_ADDRESS,
					       PCIE_INTR_FIRMWARE_MASK |
					       PCIE_INTR_CE_MASK_ALL);

		mdelay(10);
	} while (time_before(jiffies, timeout));

	if (val == 0xffffffff) {
		ath10k_err("failed to read device register, device is gone\n");
		return -EIO;
	}

	if (val & FW_IND_EVENT_PENDING) {
		ath10k_warn("device has crashed during init\n");
		ath10k_pci_write32(ar, FW_INDICATOR_ADDRESS,
				   val & ~FW_IND_EVENT_PENDING);
		ath10k_pci_hif_dump_area(ar);
		return -ECOMM;
	}

	if (!(val & FW_IND_INITIALIZED)) {
		ath10k_err("failed to receive initialized event from target: %08x\n",
			   val);
		return -ETIMEDOUT;
	}

	ath10k_dbg(ATH10K_DBG_BOOT, "boot target initialised\n");
	return 0;
}

static int ath10k_pci_cold_reset(struct ath10k *ar)
{
	int i;
	u32 val;

	ath10k_dbg(ATH10K_DBG_BOOT, "boot cold reset\n");

	/* Put Target, including PCIe, into RESET. */
	val = ath10k_pci_reg_read32(ar, SOC_GLOBAL_RESET_ADDRESS);
	val |= 1;
	ath10k_pci_reg_write32(ar, SOC_GLOBAL_RESET_ADDRESS, val);

	for (i = 0; i < ATH_PCI_RESET_WAIT_MAX; i++) {
		if (ath10k_pci_reg_read32(ar, RTC_STATE_ADDRESS) &
					  RTC_STATE_COLD_RESET_MASK)
			break;
		msleep(1);
	}

	/* Pull Target, including PCIe, out of RESET. */
	val &= ~1;
	ath10k_pci_reg_write32(ar, SOC_GLOBAL_RESET_ADDRESS, val);

	for (i = 0; i < ATH_PCI_RESET_WAIT_MAX; i++) {
		if (!(ath10k_pci_reg_read32(ar, RTC_STATE_ADDRESS) &
					    RTC_STATE_COLD_RESET_MASK))
			break;
		msleep(1);
	}

	ath10k_dbg(ATH10K_DBG_BOOT, "boot cold reset complete\n");

	return 0;
}

static int ath10k_pci_claim(struct ath10k *ar)
{
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
	struct pci_dev *pdev = ar_pci->pdev;
	u32 lcr_val;
	int ret;

	pci_set_drvdata(pdev, ar);

	ret = pci_enable_device(pdev);
	if (ret) {
		ath10k_err("failed to enable pci device: %d\n", ret);
		return ret;
	}

	ret = pci_request_region(pdev, BAR_NUM, "ath");
	if (ret) {
		ath10k_err("failed to request region BAR%d: %d\n", BAR_NUM,
			   ret);
		goto err_device;
	}

	/* Target expects 32 bit DMA. Enforce it. */
	ret = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
	if (ret) {
		ath10k_err("failed to set dma mask to 32-bit: %d\n", ret);
		goto err_region;
	}

	ret = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32));
	if (ret) {
		ath10k_err("failed to set consistent dma mask to 32-bit: %d\n",
			   ret);
		goto err_region;
	}

	pci_set_master(pdev);

	/* Workaround: Disable ASPM */
	pci_read_config_dword(pdev, 0x80, &lcr_val);
	pci_write_config_dword(pdev, 0x80, (lcr_val & 0xffffff00));

	/* Arrange for access to Target SoC registers. */
	ar_pci->mem = pci_iomap(pdev, BAR_NUM, 0);
	if (!ar_pci->mem) {
		ath10k_err("failed to iomap BAR%d\n", BAR_NUM);
		ret = -EIO;
		goto err_master;
	}

	ath10k_dbg(ATH10K_DBG_BOOT, "boot pci_mem 0x%p\n", ar_pci->mem);
	return 0;

err_master:
	pci_clear_master(pdev);

err_region:
	pci_release_region(pdev, BAR_NUM);

err_device:
	pci_disable_device(pdev);

	return ret;
}

static void ath10k_pci_release(struct ath10k *ar)
{
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
	struct pci_dev *pdev = ar_pci->pdev;

	pci_iounmap(pdev, ar_pci->mem);
	pci_release_region(pdev, BAR_NUM);
	pci_clear_master(pdev);
	pci_disable_device(pdev);
}

static int ath10k_pci_probe(struct pci_dev *pdev,
			    const struct pci_device_id *pci_dev)
{
	int ret = 0;
	struct ath10k *ar;
	struct ath10k_pci *ar_pci;
	u32 chip_id;

	ath10k_dbg(ATH10K_DBG_PCI, "pci probe\n");

	ar = ath10k_core_create(sizeof(*ar_pci), &pdev->dev,
				&ath10k_pci_hif_ops);
	if (!ar) {
		ath10k_err("failed to allocate core\n");
		return -ENOMEM;
	}

	ar_pci = ath10k_pci_priv(ar);
	ar_pci->pdev = pdev;
	ar_pci->dev = &pdev->dev;
	ar_pci->ar = ar;

	spin_lock_init(&ar_pci->ce_lock);

	ret = ath10k_pci_claim(ar);
	if (ret) {
		ath10k_err("failed to claim device: %d\n", ret);
		goto err_core_destroy;
	}

	ret = ath10k_pci_wake(ar);
	if (ret) {
		ath10k_err("failed to wake up: %d\n", ret);
		goto err_release;
	}

	chip_id = ath10k_pci_soc_read32(ar, SOC_CHIP_ID_ADDRESS);
	if (chip_id == 0xffffffff) {
		ath10k_err("failed to get chip id\n");
		goto err_sleep;
	}

	ret = ath10k_pci_alloc_ce(ar);
	if (ret) {
		ath10k_err("failed to allocate copy engine pipes: %d\n", ret);
		goto err_sleep;
	}

	ret = ath10k_core_register(ar, chip_id);
	if (ret) {
		ath10k_err("failed to register driver core: %d\n", ret);
		goto err_free_ce;
	}

	return 0;

err_free_ce:
	ath10k_pci_free_ce(ar);

err_sleep:
	ath10k_pci_sleep(ar);

err_release:
	ath10k_pci_release(ar);

err_core_destroy:
	ath10k_core_destroy(ar);

	return ret;
}

static void ath10k_pci_remove(struct pci_dev *pdev)
{
	struct ath10k *ar = pci_get_drvdata(pdev);
	struct ath10k_pci *ar_pci;

	ath10k_dbg(ATH10K_DBG_PCI, "pci remove\n");

	if (!ar)
		return;

	ar_pci = ath10k_pci_priv(ar);

	if (!ar_pci)
		return;

	ath10k_core_unregister(ar);
	ath10k_pci_free_ce(ar);
	ath10k_pci_sleep(ar);
	ath10k_pci_release(ar);
	ath10k_core_destroy(ar);
}

MODULE_DEVICE_TABLE(pci, ath10k_pci_id_table);

static struct pci_driver ath10k_pci_driver = {
	.name = "ath10k_pci",
	.id_table = ath10k_pci_id_table,
	.probe = ath10k_pci_probe,
	.remove = ath10k_pci_remove,
};

static int __init ath10k_pci_init(void)
{
	int ret;

	ret = pci_register_driver(&ath10k_pci_driver);
	if (ret)
		ath10k_err("failed to register PCI driver: %d\n", ret);

	return ret;
}
module_init(ath10k_pci_init);

static void __exit ath10k_pci_exit(void)
{
	pci_unregister_driver(&ath10k_pci_driver);
}

module_exit(ath10k_pci_exit);

MODULE_AUTHOR("Qualcomm Atheros");
MODULE_DESCRIPTION("Driver support for Atheros QCA988X PCIe devices");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_FIRMWARE(QCA988X_HW_2_0_FW_DIR "/" QCA988X_HW_2_0_FW_3_FILE);
MODULE_FIRMWARE(QCA988X_HW_2_0_FW_DIR "/" QCA988X_HW_2_0_BOARD_DATA_FILE);
