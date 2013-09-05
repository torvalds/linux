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

#include "core.h"
#include "debug.h"

#include "targaddrs.h"
#include "bmi.h"

#include "hif.h"
#include "htc.h"

#include "ce.h"
#include "pci.h"

static unsigned int ath10k_target_ps;
module_param(ath10k_target_ps, uint, 0644);
MODULE_PARM_DESC(ath10k_target_ps, "Enable ath10k Target (SoC) PS option");

#define QCA988X_2_0_DEVICE_ID	(0x003c)

static DEFINE_PCI_DEVICE_TABLE(ath10k_pci_id_table) = {
	{ PCI_VDEVICE(ATHEROS, QCA988X_2_0_DEVICE_ID) }, /* PCI-E QCA988X V2 */
	{0}
};

static int ath10k_pci_diag_read_access(struct ath10k *ar, u32 address,
				       u32 *data);

static void ath10k_pci_process_ce(struct ath10k *ar);
static int ath10k_pci_post_rx(struct ath10k *ar);
static int ath10k_pci_post_rx_pipe(struct ath10k_pci_pipe *pipe_info,
					     int num);
static void ath10k_pci_rx_pipe_cleanup(struct ath10k_pci_pipe *pipe_info);
static void ath10k_pci_stop_ce(struct ath10k *ar);
static void ath10k_pci_device_reset(struct ath10k *ar);
static int ath10k_pci_reset_target(struct ath10k *ar);
static int ath10k_pci_start_intr(struct ath10k *ar);
static void ath10k_pci_stop_intr(struct ath10k *ar);

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
	data_buf = (unsigned char *)pci_alloc_consistent(ar_pci->pdev,
							 orig_nbytes,
							 &ce_data_base);

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
		ath10k_pci_wake(ar);
		address = TARG_CPU_SPACE_TO_CE_SPACE(ar, ar_pci->mem,
						     address);
		ath10k_pci_sleep(ar);

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
		ath10k_dbg(ATH10K_DBG_PCI, "%s failure (0x%x)\n",
			   __func__, address);

	if (data_buf)
		pci_free_consistent(ar_pci->pdev, orig_nbytes,
				    data_buf, ce_data_base);

	return ret;
}

/* Read 4-byte aligned data from Target memory or register */
static int ath10k_pci_diag_read_access(struct ath10k *ar, u32 address,
				       u32 *data)
{
	/* Assume range doesn't cross this boundary */
	if (address >= DRAM_BASE_ADDRESS)
		return ath10k_pci_diag_read_mem(ar, address, data, sizeof(u32));

	ath10k_pci_wake(ar);
	*data = ath10k_pci_read32(ar, address);
	ath10k_pci_sleep(ar);
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
	data_buf = (unsigned char *)pci_alloc_consistent(ar_pci->pdev,
							 orig_nbytes,
							 &ce_data_base);
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
	ath10k_pci_wake(ar);
	address = TARG_CPU_SPACE_TO_CE_SPACE(ar, ar_pci->mem, address);
	ath10k_pci_sleep(ar);

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
		pci_free_consistent(ar_pci->pdev, orig_nbytes, data_buf,
				    ce_data_base);
	}

	if (ret != 0)
		ath10k_dbg(ATH10K_DBG_PCI, "%s failure (0x%x)\n", __func__,
			   address);

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

	ath10k_pci_wake(ar);
	ath10k_pci_write32(ar, address, data);
	ath10k_pci_sleep(ar);
	return 0;
}

static bool ath10k_pci_target_is_awake(struct ath10k *ar)
{
	void __iomem *mem = ath10k_pci_priv(ar)->mem;
	u32 val;
	val = ioread32(mem + PCIE_LOCAL_BASE_ADDRESS +
		       RTC_STATE_ADDRESS);
	return (RTC_STATE_V_GET(val) == RTC_STATE_V_ON);
}

static void ath10k_pci_wait(struct ath10k *ar)
{
	int n = 100;

	while (n-- && !ath10k_pci_target_is_awake(ar))
		msleep(10);

	if (n < 0)
		ath10k_warn("Unable to wakeup target\n");
}

int ath10k_do_pci_wake(struct ath10k *ar)
{
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
	void __iomem *pci_addr = ar_pci->mem;
	int tot_delay = 0;
	int curr_delay = 5;

	if (atomic_read(&ar_pci->keep_awake_count) == 0) {
		/* Force AWAKE */
		iowrite32(PCIE_SOC_WAKE_V_MASK,
			  pci_addr + PCIE_LOCAL_BASE_ADDRESS +
			  PCIE_SOC_WAKE_ADDRESS);
	}
	atomic_inc(&ar_pci->keep_awake_count);

	if (ar_pci->verified_awake)
		return 0;

	for (;;) {
		if (ath10k_pci_target_is_awake(ar)) {
			ar_pci->verified_awake = true;
			return 0;
		}

		if (tot_delay > PCIE_WAKE_TIMEOUT) {
			ath10k_warn("target took longer %d us to wake up (awake count %d)\n",
				    PCIE_WAKE_TIMEOUT,
				    atomic_read(&ar_pci->keep_awake_count));
			return -ETIMEDOUT;
		}

		udelay(curr_delay);
		tot_delay += curr_delay;

		if (curr_delay < 50)
			curr_delay += 5;
	}
}

void ath10k_do_pci_sleep(struct ath10k *ar)
{
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
	void __iomem *pci_addr = ar_pci->mem;

	if (atomic_dec_and_test(&ar_pci->keep_awake_count)) {
		/* Allow sleep */
		ar_pci->verified_awake = false;
		iowrite32(PCIE_SOC_WAKE_RESET,
			  pci_addr + PCIE_LOCAL_BASE_ADDRESS +
			  PCIE_SOC_WAKE_ADDRESS);
	}
}

/*
 * FIXME: Handle OOM properly.
 */
static inline
struct ath10k_pci_compl *get_free_compl(struct ath10k_pci_pipe *pipe_info)
{
	struct ath10k_pci_compl *compl = NULL;

	spin_lock_bh(&pipe_info->pipe_lock);
	if (list_empty(&pipe_info->compl_free)) {
		ath10k_warn("Completion buffers are full\n");
		goto exit;
	}
	compl = list_first_entry(&pipe_info->compl_free,
				 struct ath10k_pci_compl, list);
	list_del(&compl->list);
exit:
	spin_unlock_bh(&pipe_info->pipe_lock);
	return compl;
}

/* Called by lower (CE) layer when a send to Target completes. */
static void ath10k_pci_ce_send_done(struct ath10k_ce_pipe *ce_state,
				    void *transfer_context,
				    u32 ce_data,
				    unsigned int nbytes,
				    unsigned int transfer_id)
{
	struct ath10k *ar = ce_state->ar;
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
	struct ath10k_pci_pipe *pipe_info =  &ar_pci->pipe_info[ce_state->id];
	struct ath10k_pci_compl *compl;
	bool process = false;

	do {
		/*
		 * For the send completion of an item in sendlist, just
		 * increment num_sends_allowed. The upper layer callback will
		 * be triggered when last fragment is done with send.
		 */
		if (transfer_context == CE_SENDLIST_ITEM_CTXT) {
			spin_lock_bh(&pipe_info->pipe_lock);
			pipe_info->num_sends_allowed++;
			spin_unlock_bh(&pipe_info->pipe_lock);
			continue;
		}

		compl = get_free_compl(pipe_info);
		if (!compl)
			break;

		compl->state = ATH10K_PCI_COMPL_SEND;
		compl->ce_state = ce_state;
		compl->pipe_info = pipe_info;
		compl->skb = transfer_context;
		compl->nbytes = nbytes;
		compl->transfer_id = transfer_id;
		compl->flags = 0;

		/*
		 * Add the completion to the processing queue.
		 */
		spin_lock_bh(&ar_pci->compl_lock);
		list_add_tail(&compl->list, &ar_pci->compl_process);
		spin_unlock_bh(&ar_pci->compl_lock);

		process = true;
	} while (ath10k_ce_completed_send_next(ce_state,
							   &transfer_context,
							   &ce_data, &nbytes,
							   &transfer_id) == 0);

	/*
	 * If only some of the items within a sendlist have completed,
	 * don't invoke completion processing until the entire sendlist
	 * has been sent.
	 */
	if (!process)
		return;

	ath10k_pci_process_ce(ar);
}

/* Called by lower (CE) layer when data is received from the Target. */
static void ath10k_pci_ce_recv_data(struct ath10k_ce_pipe *ce_state,
				    void *transfer_context, u32 ce_data,
				    unsigned int nbytes,
				    unsigned int transfer_id,
				    unsigned int flags)
{
	struct ath10k *ar = ce_state->ar;
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
	struct ath10k_pci_pipe *pipe_info =  &ar_pci->pipe_info[ce_state->id];
	struct ath10k_pci_compl *compl;
	struct sk_buff *skb;

	do {
		compl = get_free_compl(pipe_info);
		if (!compl)
			break;

		compl->state = ATH10K_PCI_COMPL_RECV;
		compl->ce_state = ce_state;
		compl->pipe_info = pipe_info;
		compl->skb = transfer_context;
		compl->nbytes = nbytes;
		compl->transfer_id = transfer_id;
		compl->flags = flags;

		skb = transfer_context;
		dma_unmap_single(ar->dev, ATH10K_SKB_CB(skb)->paddr,
				 skb->len + skb_tailroom(skb),
				 DMA_FROM_DEVICE);
		/*
		 * Add the completion to the processing queue.
		 */
		spin_lock_bh(&ar_pci->compl_lock);
		list_add_tail(&compl->list, &ar_pci->compl_process);
		spin_unlock_bh(&ar_pci->compl_lock);

	} while (ath10k_ce_completed_recv_next(ce_state,
							   &transfer_context,
							   &ce_data, &nbytes,
							   &transfer_id,
							   &flags) == 0);

	ath10k_pci_process_ce(ar);
}

/* Send the first nbytes bytes of the buffer */
static int ath10k_pci_hif_send_head(struct ath10k *ar, u8 pipe_id,
				    unsigned int transfer_id,
				    unsigned int bytes, struct sk_buff *nbuf)
{
	struct ath10k_skb_cb *skb_cb = ATH10K_SKB_CB(nbuf);
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
	struct ath10k_pci_pipe *pipe_info = &(ar_pci->pipe_info[pipe_id]);
	struct ath10k_ce_pipe *ce_hdl = pipe_info->ce_hdl;
	struct ce_sendlist sendlist;
	unsigned int len;
	u32 flags = 0;
	int ret;

	memset(&sendlist, 0, sizeof(struct ce_sendlist));

	len = min(bytes, nbuf->len);
	bytes -= len;

	if (len & 3)
		ath10k_warn("skb not aligned to 4-byte boundary (%d)\n", len);

	ath10k_dbg(ATH10K_DBG_PCI,
		   "pci send data vaddr %p paddr 0x%llx len %d as %d bytes\n",
		   nbuf->data, (unsigned long long) skb_cb->paddr,
		   nbuf->len, len);
	ath10k_dbg_dump(ATH10K_DBG_PCI_DUMP, NULL,
			"ath10k tx: data: ",
			nbuf->data, nbuf->len);

	ath10k_ce_sendlist_buf_add(&sendlist, skb_cb->paddr, len, flags);

	/* Make sure we have resources to handle this request */
	spin_lock_bh(&pipe_info->pipe_lock);
	if (!pipe_info->num_sends_allowed) {
		ath10k_warn("Pipe: %d is full\n", pipe_id);
		spin_unlock_bh(&pipe_info->pipe_lock);
		return -ENOSR;
	}
	pipe_info->num_sends_allowed--;
	spin_unlock_bh(&pipe_info->pipe_lock);

	ret = ath10k_ce_sendlist_send(ce_hdl, nbuf, &sendlist, transfer_id);
	if (ret)
		ath10k_warn("CE send failed: %p\n", nbuf);

	return ret;
}

static u16 ath10k_pci_hif_get_free_queue_number(struct ath10k *ar, u8 pipe)
{
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
	struct ath10k_pci_pipe *pipe_info = &(ar_pci->pipe_info[pipe]);
	int ret;

	spin_lock_bh(&pipe_info->pipe_lock);
	ret = pipe_info->num_sends_allowed;
	spin_unlock_bh(&pipe_info->pipe_lock);

	return ret;
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
	ath10k_err("firmware version: %u.%u.%u.%u\n", ar->fw_version_major,
		   ar->fw_version_minor, ar->fw_version_release,
		   ar->fw_version_build);

	host_addr = host_interest_item_address(HI_ITEM(hi_failure_state));
	if (ath10k_pci_diag_read_mem(ar, host_addr,
				     &reg_dump_area, sizeof(u32)) != 0) {
		ath10k_warn("could not read hi_failure_state\n");
		return;
	}

	ath10k_err("target register Dump Location: 0x%08X\n", reg_dump_area);

	ret = ath10k_pci_diag_read_mem(ar, reg_dump_area,
				       &reg_dump_values[0],
				       REG_DUMP_COUNT_QCA988X * sizeof(u32));
	if (ret != 0) {
		ath10k_err("could not dump FW Dump Area\n");
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

	ieee80211_queue_work(ar->hw, &ar->restart_work);
}

static void ath10k_pci_hif_send_complete_check(struct ath10k *ar, u8 pipe,
					       int force)
{
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

	ath10k_dbg(ATH10K_DBG_PCI, "%s\n", __func__);

	memcpy(&ar_pci->msg_callbacks_current, callbacks,
	       sizeof(ar_pci->msg_callbacks_current));
}

static int ath10k_pci_start_ce(struct ath10k *ar)
{
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
	struct ath10k_ce_pipe *ce_diag = ar_pci->ce_diag;
	const struct ce_attr *attr;
	struct ath10k_pci_pipe *pipe_info;
	struct ath10k_pci_compl *compl;
	int i, pipe_num, completions, disable_interrupts;

	spin_lock_init(&ar_pci->compl_lock);
	INIT_LIST_HEAD(&ar_pci->compl_process);

	for (pipe_num = 0; pipe_num < ar_pci->ce_count; pipe_num++) {
		pipe_info = &ar_pci->pipe_info[pipe_num];

		spin_lock_init(&pipe_info->pipe_lock);
		INIT_LIST_HEAD(&pipe_info->compl_free);

		/* Handle Diagnostic CE specially */
		if (pipe_info->ce_hdl == ce_diag)
			continue;

		attr = &host_ce_config_wlan[pipe_num];
		completions = 0;

		if (attr->src_nentries) {
			disable_interrupts = attr->flags & CE_ATTR_DIS_INTR;
			ath10k_ce_send_cb_register(pipe_info->ce_hdl,
						   ath10k_pci_ce_send_done,
						   disable_interrupts);
			completions += attr->src_nentries;
			pipe_info->num_sends_allowed = attr->src_nentries - 1;
		}

		if (attr->dest_nentries) {
			ath10k_ce_recv_cb_register(pipe_info->ce_hdl,
						   ath10k_pci_ce_recv_data);
			completions += attr->dest_nentries;
		}

		if (completions == 0)
			continue;

		for (i = 0; i < completions; i++) {
			compl = kmalloc(sizeof(*compl), GFP_KERNEL);
			if (!compl) {
				ath10k_warn("No memory for completion state\n");
				ath10k_pci_stop_ce(ar);
				return -ENOMEM;
			}

			compl->state = ATH10K_PCI_COMPL_FREE;
			list_add_tail(&compl->list, &pipe_info->compl_free);
		}
	}

	return 0;
}

static void ath10k_pci_stop_ce(struct ath10k *ar)
{
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
	struct ath10k_pci_compl *compl;
	struct sk_buff *skb;
	int i;

	ath10k_ce_disable_interrupts(ar);

	/* Cancel the pending tasklet */
	tasklet_kill(&ar_pci->intr_tq);

	for (i = 0; i < CE_COUNT; i++)
		tasklet_kill(&ar_pci->pipe_info[i].intr);

	/* Mark pending completions as aborted, so that upper layers free up
	 * their associated resources */
	spin_lock_bh(&ar_pci->compl_lock);
	list_for_each_entry(compl, &ar_pci->compl_process, list) {
		skb = compl->skb;
		ATH10K_SKB_CB(skb)->is_aborted = true;
	}
	spin_unlock_bh(&ar_pci->compl_lock);
}

static void ath10k_pci_cleanup_ce(struct ath10k *ar)
{
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
	struct ath10k_pci_compl *compl, *tmp;
	struct ath10k_pci_pipe *pipe_info;
	struct sk_buff *netbuf;
	int pipe_num;

	/* Free pending completions. */
	spin_lock_bh(&ar_pci->compl_lock);
	if (!list_empty(&ar_pci->compl_process))
		ath10k_warn("pending completions still present! possible memory leaks.\n");

	list_for_each_entry_safe(compl, tmp, &ar_pci->compl_process, list) {
		list_del(&compl->list);
		netbuf = compl->skb;
		dev_kfree_skb_any(netbuf);
		kfree(compl);
	}
	spin_unlock_bh(&ar_pci->compl_lock);

	/* Free unused completions for each pipe. */
	for (pipe_num = 0; pipe_num < ar_pci->ce_count; pipe_num++) {
		pipe_info = &ar_pci->pipe_info[pipe_num];

		spin_lock_bh(&pipe_info->pipe_lock);
		list_for_each_entry_safe(compl, tmp,
					 &pipe_info->compl_free, list) {
			list_del(&compl->list);
			kfree(compl);
		}
		spin_unlock_bh(&pipe_info->pipe_lock);
	}
}

static void ath10k_pci_process_ce(struct ath10k *ar)
{
	struct ath10k_pci *ar_pci = ar->hif.priv;
	struct ath10k_hif_cb *cb = &ar_pci->msg_callbacks_current;
	struct ath10k_pci_compl *compl;
	struct sk_buff *skb;
	unsigned int nbytes;
	int ret, send_done = 0;

	/* Upper layers aren't ready to handle tx/rx completions in parallel so
	 * we must serialize all completion processing. */

	spin_lock_bh(&ar_pci->compl_lock);
	if (ar_pci->compl_processing) {
		spin_unlock_bh(&ar_pci->compl_lock);
		return;
	}
	ar_pci->compl_processing = true;
	spin_unlock_bh(&ar_pci->compl_lock);

	for (;;) {
		spin_lock_bh(&ar_pci->compl_lock);
		if (list_empty(&ar_pci->compl_process)) {
			spin_unlock_bh(&ar_pci->compl_lock);
			break;
		}
		compl = list_first_entry(&ar_pci->compl_process,
					 struct ath10k_pci_compl, list);
		list_del(&compl->list);
		spin_unlock_bh(&ar_pci->compl_lock);

		switch (compl->state) {
		case ATH10K_PCI_COMPL_SEND:
			cb->tx_completion(ar,
					  compl->skb,
					  compl->transfer_id);
			send_done = 1;
			break;
		case ATH10K_PCI_COMPL_RECV:
			ret = ath10k_pci_post_rx_pipe(compl->pipe_info, 1);
			if (ret) {
				ath10k_warn("Unable to post recv buffer for pipe: %d\n",
					    compl->pipe_info->pipe_num);
				break;
			}

			skb = compl->skb;
			nbytes = compl->nbytes;

			ath10k_dbg(ATH10K_DBG_PCI,
				   "ath10k_pci_ce_recv_data netbuf=%p  nbytes=%d\n",
				   skb, nbytes);
			ath10k_dbg_dump(ATH10K_DBG_PCI_DUMP, NULL,
					"ath10k rx: ", skb->data, nbytes);

			if (skb->len + skb_tailroom(skb) >= nbytes) {
				skb_trim(skb, 0);
				skb_put(skb, nbytes);
				cb->rx_completion(ar, skb,
						  compl->pipe_info->pipe_num);
			} else {
				ath10k_warn("rxed more than expected (nbytes %d, max %d)",
					    nbytes,
					    skb->len + skb_tailroom(skb));
			}
			break;
		case ATH10K_PCI_COMPL_FREE:
			ath10k_warn("free completion cannot be processed\n");
			break;
		default:
			ath10k_warn("invalid completion state (%d)\n",
				    compl->state);
			break;
		}

		compl->state = ATH10K_PCI_COMPL_FREE;

		/*
		 * Add completion back to the pipe's free list.
		 */
		spin_lock_bh(&compl->pipe_info->pipe_lock);
		list_add_tail(&compl->list, &compl->pipe_info->compl_free);
		compl->pipe_info->num_sends_allowed += send_done;
		spin_unlock_bh(&compl->pipe_info->pipe_lock);
	}

	spin_lock_bh(&ar_pci->compl_lock);
	ar_pci->compl_processing = false;
	spin_unlock_bh(&ar_pci->compl_lock);
}

/* TODO - temporary mapping while we have too few CE's */
static int ath10k_pci_hif_map_service_to_pipe(struct ath10k *ar,
					      u16 service_id, u8 *ul_pipe,
					      u8 *dl_pipe, int *ul_is_polled,
					      int *dl_is_polled)
{
	int ret = 0;

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
			ath10k_warn("could not allocate skbuff for pipe %d\n",
				    num);
			ret = -ENOMEM;
			goto err;
		}

		WARN_ONCE((unsigned long)skb->data & 3, "unaligned skb");

		ce_data = dma_map_single(ar->dev, skb->data,
					 skb->len + skb_tailroom(skb),
					 DMA_FROM_DEVICE);

		if (unlikely(dma_mapping_error(ar->dev, ce_data))) {
			ath10k_warn("could not dma map skbuff\n");
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
			ath10k_warn("could not enqueue to pipe %d (%d)\n",
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

	for (pipe_num = 0; pipe_num < ar_pci->ce_count; pipe_num++) {
		pipe_info = &ar_pci->pipe_info[pipe_num];
		attr = &host_ce_config_wlan[pipe_num];

		if (attr->dest_nentries == 0)
			continue;

		ret = ath10k_pci_post_rx_pipe(pipe_info,
					      attr->dest_nentries - 1);
		if (ret) {
			ath10k_warn("Unable to replenish recv buffers for pipe: %d\n",
				    pipe_num);

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
	int ret;

	ret = ath10k_pci_start_ce(ar);
	if (ret) {
		ath10k_warn("could not start CE (%d)\n", ret);
		return ret;
	}

	/* Post buffers once to start things off. */
	ret = ath10k_pci_post_rx(ar);
	if (ret) {
		ath10k_warn("could not post rx pipes (%d)\n", ret);
		return ret;
	}

	ar_pci->started = 1;
	return 0;
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
		if (netbuf != CE_SENDLIST_ITEM_CTXT) {
			/*
			 * Indicate the completion to higer layer to free
			 * the buffer
			 */
			ATH10K_SKB_CB(netbuf)->is_aborted = true;
			ar_pci->msg_callbacks_current.tx_completion(ar,
								    netbuf,
								    id);
		}
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

	for (pipe_num = 0; pipe_num < ar_pci->ce_count; pipe_num++) {
		struct ath10k_pci_pipe *pipe_info;

		pipe_info = &ar_pci->pipe_info[pipe_num];
		ath10k_pci_rx_pipe_cleanup(pipe_info);
		ath10k_pci_tx_pipe_cleanup(pipe_info);
	}
}

static void ath10k_pci_ce_deinit(struct ath10k *ar)
{
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
	struct ath10k_pci_pipe *pipe_info;
	int pipe_num;

	for (pipe_num = 0; pipe_num < ar_pci->ce_count; pipe_num++) {
		pipe_info = &ar_pci->pipe_info[pipe_num];
		if (pipe_info->ce_hdl) {
			ath10k_ce_deinit(pipe_info->ce_hdl);
			pipe_info->ce_hdl = NULL;
			pipe_info->buf_sz = 0;
		}
	}
}

static void ath10k_pci_disable_irqs(struct ath10k *ar)
{
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
	int i;

	for (i = 0; i < max(1, ar_pci->num_msi_intrs); i++)
		disable_irq(ar_pci->pdev->irq + i);
}

static void ath10k_pci_hif_stop(struct ath10k *ar)
{
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);

	ath10k_dbg(ATH10K_DBG_PCI, "%s\n", __func__);

	/* Irqs are never explicitly re-enabled. They are implicitly re-enabled
	 * by ath10k_pci_start_intr(). */
	ath10k_pci_disable_irqs(ar);

	ath10k_pci_stop_ce(ar);

	/* At this point, asynchronous threads are stopped, the target should
	 * not DMA nor interrupt. We process the leftovers and then free
	 * everything else up. */

	ath10k_pci_process_ce(ar);
	ath10k_pci_cleanup_ce(ar);
	ath10k_pci_buffer_cleanup(ar);

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

	init_completion(&xfer.done);

	ret = ath10k_ce_send(ce_tx, &xfer, req_paddr, req_len, -1, 0);
	if (ret)
		goto err_resp;

	ret = wait_for_completion_timeout(&xfer.done,
					  BMI_COMMUNICATION_TIMEOUT_HZ);
	if (ret <= 0) {
		u32 unused_buffer;
		unsigned int unused_nbytes;
		unsigned int unused_id;

		ret = -ETIMEDOUT;
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

static void ath10k_pci_bmi_send_done(struct ath10k_ce_pipe *ce_state,
				     void *transfer_context,
				     u32 data,
				     unsigned int nbytes,
				     unsigned int transfer_id)
{
	struct bmi_xfer *xfer = transfer_context;

	if (xfer->wait_for_resp)
		return;

	complete(&xfer->done);
}

static void ath10k_pci_bmi_recv_data(struct ath10k_ce_pipe *ce_state,
				     void *transfer_context,
				     u32 data,
				     unsigned int nbytes,
				     unsigned int transfer_id,
				     unsigned int flags)
{
	struct bmi_xfer *xfer = transfer_context;

	if (!xfer->wait_for_resp) {
		ath10k_warn("unexpected: BMI data received; ignoring\n");
		return;
	}

	xfer->resp_len = nbytes;
	complete(&xfer->done);
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
		ath10k_warn("Unable to read core ctrl\n");
		return ret;
	}

	/* A_INUM_FIRMWARE interrupt to Target CPU */
	core_ctrl |= CORE_CTRL_CPU_INTR_MASK;

	ret = ath10k_pci_diag_write_access(ar, SOC_CORE_BASE_ADDRESS |
					       CORE_CTRL_ADDRESS,
					   core_ctrl);
	if (ret)
		ath10k_warn("Unable to set interrupt mask\n");

	return ret;
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



static int ath10k_pci_ce_init(struct ath10k *ar)
{
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
	struct ath10k_pci_pipe *pipe_info;
	const struct ce_attr *attr;
	int pipe_num;

	for (pipe_num = 0; pipe_num < ar_pci->ce_count; pipe_num++) {
		pipe_info = &ar_pci->pipe_info[pipe_num];
		pipe_info->pipe_num = pipe_num;
		pipe_info->hif_ce_state = ar;
		attr = &host_ce_config_wlan[pipe_num];

		pipe_info->ce_hdl = ath10k_ce_init(ar, pipe_num, attr);
		if (pipe_info->ce_hdl == NULL) {
			ath10k_err("Unable to initialize CE for pipe: %d\n",
				   pipe_num);

			/* It is safe to call it here. It checks if ce_hdl is
			 * valid for each pipe */
			ath10k_pci_ce_deinit(ar);
			return -1;
		}

		if (pipe_num == ar_pci->ce_count - 1) {
			/*
			 * Reserve the ultimate CE for
			 * diagnostic Window support
			 */
			ar_pci->ce_diag =
			ar_pci->pipe_info[ar_pci->ce_count - 1].ce_hdl;
			continue;
		}

		pipe_info->buf_sz = (size_t) (attr->src_sz_max);
	}

	/*
	 * Initially, establish CE completion handlers for use with BMI.
	 * These are overwritten with generic handlers after we exit BMI phase.
	 */
	pipe_info = &ar_pci->pipe_info[BMI_CE_NUM_TO_TARG];
	ath10k_ce_send_cb_register(pipe_info->ce_hdl,
				   ath10k_pci_bmi_send_done, 0);

	pipe_info = &ar_pci->pipe_info[BMI_CE_NUM_TO_HOST];
	ath10k_ce_recv_cb_register(pipe_info->ce_hdl,
				   ath10k_pci_bmi_recv_data);

	return 0;
}

static void ath10k_pci_fw_interrupt_handler(struct ath10k *ar)
{
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
	u32 fw_indicator_address, fw_indicator;

	ath10k_pci_wake(ar);

	fw_indicator_address = ar_pci->fw_indicator_address;
	fw_indicator = ath10k_pci_read32(ar, fw_indicator_address);

	if (fw_indicator & FW_IND_EVENT_PENDING) {
		/* ACK: clear Target-side pending event */
		ath10k_pci_write32(ar, fw_indicator_address,
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

	ath10k_pci_sleep(ar);
}

static int ath10k_pci_hif_power_up(struct ath10k *ar)
{
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
	int ret;

	ret = ath10k_pci_start_intr(ar);
	if (ret) {
		ath10k_err("could not start interrupt handling (%d)\n", ret);
		goto err;
	}

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
	ath10k_pci_device_reset(ar);

	ret = ath10k_pci_reset_target(ar);
	if (ret)
		goto err_irq;

	if (!test_bit(ATH10K_PCI_FEATURE_SOC_POWER_SAVE, ar_pci->features))
		/* Force AWAKE forever */
		ath10k_do_pci_wake(ar);

	ret = ath10k_pci_ce_init(ar);
	if (ret)
		goto err_ps;

	ret = ath10k_pci_init_config(ar);
	if (ret)
		goto err_ce;

	ret = ath10k_pci_wake_target_cpu(ar);
	if (ret) {
		ath10k_err("could not wake up target CPU (%d)\n", ret);
		goto err_ce;
	}

	return 0;

err_ce:
	ath10k_pci_ce_deinit(ar);
err_ps:
	if (!test_bit(ATH10K_PCI_FEATURE_SOC_POWER_SAVE, ar_pci->features))
		ath10k_do_pci_sleep(ar);
err_irq:
	ath10k_pci_stop_intr(ar);
err:
	return ret;
}

static void ath10k_pci_hif_power_down(struct ath10k *ar)
{
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);

	ath10k_pci_stop_intr(ar);

	ath10k_pci_ce_deinit(ar);
	if (!test_bit(ATH10K_PCI_FEATURE_SOC_POWER_SAVE, ar_pci->features))
		ath10k_do_pci_sleep(ar);
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
	.send_head		= ath10k_pci_hif_send_head,
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
		/*
		 * IMPORTANT: INTR_CLR regiser has to be set after
		 * INTR_ENABLE is set to 0, otherwise interrupt can not be
		 * really cleared.
		 */
		iowrite32(0, ar_pci->mem +
			  (SOC_CORE_BASE_ADDRESS |
			   PCIE_INTR_ENABLE_ADDRESS));
		iowrite32(PCIE_INTR_FIRMWARE_MASK |
			  PCIE_INTR_CE_MASK_ALL,
			  ar_pci->mem + (SOC_CORE_BASE_ADDRESS |
					 PCIE_INTR_CLR_ADDRESS));
		/*
		 * IMPORTANT: this extra read transaction is required to
		 * flush the posted write buffer.
		 */
		(void) ioread32(ar_pci->mem +
				(SOC_CORE_BASE_ADDRESS |
				 PCIE_INTR_ENABLE_ADDRESS));
	}

	tasklet_schedule(&ar_pci->intr_tq);

	return IRQ_HANDLED;
}

static void ath10k_pci_tasklet(unsigned long data)
{
	struct ath10k *ar = (struct ath10k *)data;
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);

	ath10k_pci_fw_interrupt_handler(ar); /* FIXME: Handle FW error */
	ath10k_ce_per_engine_service_any(ar);

	if (ar_pci->num_msi_intrs == 0) {
		/* Enable Legacy PCI line interrupts */
		iowrite32(PCIE_INTR_FIRMWARE_MASK |
			  PCIE_INTR_CE_MASK_ALL,
			  ar_pci->mem + (SOC_CORE_BASE_ADDRESS |
					 PCIE_INTR_ENABLE_ADDRESS));
		/*
		 * IMPORTANT: this extra read transaction is required to
		 * flush the posted write buffer
		 */
		(void) ioread32(ar_pci->mem +
				(SOC_CORE_BASE_ADDRESS |
				 PCIE_INTR_ENABLE_ADDRESS));
	}
}

static int ath10k_pci_start_intr_msix(struct ath10k *ar, int num)
{
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
	int ret;
	int i;

	ret = pci_enable_msi_block(ar_pci->pdev, num);
	if (ret)
		return ret;

	ret = request_irq(ar_pci->pdev->irq + MSI_ASSIGN_FW,
			  ath10k_pci_msi_fw_handler,
			  IRQF_SHARED, "ath10k_pci", ar);
	if (ret) {
		ath10k_warn("request_irq(%d) failed %d\n",
			    ar_pci->pdev->irq + MSI_ASSIGN_FW, ret);

		pci_disable_msi(ar_pci->pdev);
		return ret;
	}

	for (i = MSI_ASSIGN_CE_INITIAL; i <= MSI_ASSIGN_CE_MAX; i++) {
		ret = request_irq(ar_pci->pdev->irq + i,
				  ath10k_pci_per_engine_handler,
				  IRQF_SHARED, "ath10k_pci", ar);
		if (ret) {
			ath10k_warn("request_irq(%d) failed %d\n",
				    ar_pci->pdev->irq + i, ret);

			for (i--; i >= MSI_ASSIGN_CE_INITIAL; i--)
				free_irq(ar_pci->pdev->irq + i, ar);

			free_irq(ar_pci->pdev->irq + MSI_ASSIGN_FW, ar);
			pci_disable_msi(ar_pci->pdev);
			return ret;
		}
	}

	ath10k_info("MSI-X interrupt handling (%d intrs)\n", num);
	return 0;
}

static int ath10k_pci_start_intr_msi(struct ath10k *ar)
{
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
	int ret;

	ret = pci_enable_msi(ar_pci->pdev);
	if (ret < 0)
		return ret;

	ret = request_irq(ar_pci->pdev->irq,
			  ath10k_pci_interrupt_handler,
			  IRQF_SHARED, "ath10k_pci", ar);
	if (ret < 0) {
		pci_disable_msi(ar_pci->pdev);
		return ret;
	}

	ath10k_info("MSI interrupt handling\n");
	return 0;
}

static int ath10k_pci_start_intr_legacy(struct ath10k *ar)
{
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
	int ret;

	ret = request_irq(ar_pci->pdev->irq,
			  ath10k_pci_interrupt_handler,
			  IRQF_SHARED, "ath10k_pci", ar);
	if (ret < 0)
		return ret;

	/*
	 * Make sure to wake the Target before enabling Legacy
	 * Interrupt.
	 */
	iowrite32(PCIE_SOC_WAKE_V_MASK,
		  ar_pci->mem + PCIE_LOCAL_BASE_ADDRESS +
		  PCIE_SOC_WAKE_ADDRESS);

	ath10k_pci_wait(ar);

	/*
	 * A potential race occurs here: The CORE_BASE write
	 * depends on target correctly decoding AXI address but
	 * host won't know when target writes BAR to CORE_CTRL.
	 * This write might get lost if target has NOT written BAR.
	 * For now, fix the race by repeating the write in below
	 * synchronization checking.
	 */
	iowrite32(PCIE_INTR_FIRMWARE_MASK |
		  PCIE_INTR_CE_MASK_ALL,
		  ar_pci->mem + (SOC_CORE_BASE_ADDRESS |
				 PCIE_INTR_ENABLE_ADDRESS));
	iowrite32(PCIE_SOC_WAKE_RESET,
		  ar_pci->mem + PCIE_LOCAL_BASE_ADDRESS +
		  PCIE_SOC_WAKE_ADDRESS);

	ath10k_info("legacy interrupt handling\n");
	return 0;
}

static int ath10k_pci_start_intr(struct ath10k *ar)
{
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
	int num = MSI_NUM_REQUEST;
	int ret;
	int i;

	tasklet_init(&ar_pci->intr_tq, ath10k_pci_tasklet, (unsigned long) ar);
	tasklet_init(&ar_pci->msi_fw_err, ath10k_msi_err_tasklet,
		     (unsigned long) ar);

	for (i = 0; i < CE_COUNT; i++) {
		ar_pci->pipe_info[i].ar_pci = ar_pci;
		tasklet_init(&ar_pci->pipe_info[i].intr,
			     ath10k_pci_ce_tasklet,
			     (unsigned long)&ar_pci->pipe_info[i]);
	}

	if (!test_bit(ATH10K_PCI_FEATURE_MSI_X, ar_pci->features))
		num = 1;

	if (num > 1) {
		ret = ath10k_pci_start_intr_msix(ar, num);
		if (ret == 0)
			goto exit;

		ath10k_warn("MSI-X didn't succeed (%d), trying MSI\n", ret);
		num = 1;
	}

	if (num == 1) {
		ret = ath10k_pci_start_intr_msi(ar);
		if (ret == 0)
			goto exit;

		ath10k_warn("MSI didn't succeed (%d), trying legacy INTR\n",
			    ret);
		num = 0;
	}

	ret = ath10k_pci_start_intr_legacy(ar);

exit:
	ar_pci->num_msi_intrs = num;
	ar_pci->ce_count = CE_COUNT;
	return ret;
}

static void ath10k_pci_stop_intr(struct ath10k *ar)
{
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
	int i;

	/* There's at least one interrupt irregardless whether its legacy INTR
	 * or MSI or MSI-X */
	for (i = 0; i < max(1, ar_pci->num_msi_intrs); i++)
		free_irq(ar_pci->pdev->irq + i, ar);

	if (ar_pci->num_msi_intrs > 0)
		pci_disable_msi(ar_pci->pdev);
}

static int ath10k_pci_reset_target(struct ath10k *ar)
{
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
	int wait_limit = 300; /* 3 sec */

	/* Wait for Target to finish initialization before we proceed. */
	iowrite32(PCIE_SOC_WAKE_V_MASK,
		  ar_pci->mem + PCIE_LOCAL_BASE_ADDRESS +
		  PCIE_SOC_WAKE_ADDRESS);

	ath10k_pci_wait(ar);

	while (wait_limit-- &&
	       !(ioread32(ar_pci->mem + FW_INDICATOR_ADDRESS) &
		 FW_IND_INITIALIZED)) {
		if (ar_pci->num_msi_intrs == 0)
			/* Fix potential race by repeating CORE_BASE writes */
			iowrite32(PCIE_INTR_FIRMWARE_MASK |
				  PCIE_INTR_CE_MASK_ALL,
				  ar_pci->mem + (SOC_CORE_BASE_ADDRESS |
						 PCIE_INTR_ENABLE_ADDRESS));
		mdelay(10);
	}

	if (wait_limit < 0) {
		ath10k_err("Target stalled\n");
		iowrite32(PCIE_SOC_WAKE_RESET,
			  ar_pci->mem + PCIE_LOCAL_BASE_ADDRESS +
			  PCIE_SOC_WAKE_ADDRESS);
		return -EIO;
	}

	iowrite32(PCIE_SOC_WAKE_RESET,
		  ar_pci->mem + PCIE_LOCAL_BASE_ADDRESS +
		  PCIE_SOC_WAKE_ADDRESS);

	return 0;
}

static void ath10k_pci_device_reset(struct ath10k *ar)
{
	int i;
	u32 val;

	if (!SOC_GLOBAL_RESET_ADDRESS)
		return;

	ath10k_pci_reg_write32(ar, PCIE_SOC_WAKE_ADDRESS,
			       PCIE_SOC_WAKE_V_MASK);
	for (i = 0; i < ATH_PCI_RESET_WAIT_MAX; i++) {
		if (ath10k_pci_target_is_awake(ar))
			break;
		msleep(1);
	}

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

	ath10k_pci_reg_write32(ar, PCIE_SOC_WAKE_ADDRESS, PCIE_SOC_WAKE_RESET);
}

static void ath10k_pci_dump_features(struct ath10k_pci *ar_pci)
{
	int i;

	for (i = 0; i < ATH10K_PCI_FEATURE_COUNT; i++) {
		if (!test_bit(i, ar_pci->features))
			continue;

		switch (i) {
		case ATH10K_PCI_FEATURE_MSI_X:
			ath10k_dbg(ATH10K_DBG_PCI, "device supports MSI-X\n");
			break;
		case ATH10K_PCI_FEATURE_SOC_POWER_SAVE:
			ath10k_dbg(ATH10K_DBG_PCI, "QCA98XX SoC power save enabled\n");
			break;
		}
	}
}

static int ath10k_pci_probe(struct pci_dev *pdev,
			    const struct pci_device_id *pci_dev)
{
	void __iomem *mem;
	int ret = 0;
	struct ath10k *ar;
	struct ath10k_pci *ar_pci;
	u32 lcr_val, chip_id;

	ath10k_dbg(ATH10K_DBG_PCI, "%s\n", __func__);

	ar_pci = kzalloc(sizeof(*ar_pci), GFP_KERNEL);
	if (ar_pci == NULL)
		return -ENOMEM;

	ar_pci->pdev = pdev;
	ar_pci->dev = &pdev->dev;

	switch (pci_dev->device) {
	case QCA988X_2_0_DEVICE_ID:
		set_bit(ATH10K_PCI_FEATURE_MSI_X, ar_pci->features);
		break;
	default:
		ret = -ENODEV;
		ath10k_err("Unkown device ID: %d\n", pci_dev->device);
		goto err_ar_pci;
	}

	if (ath10k_target_ps)
		set_bit(ATH10K_PCI_FEATURE_SOC_POWER_SAVE, ar_pci->features);

	ath10k_pci_dump_features(ar_pci);

	ar = ath10k_core_create(ar_pci, ar_pci->dev, &ath10k_pci_hif_ops);
	if (!ar) {
		ath10k_err("ath10k_core_create failed!\n");
		ret = -EINVAL;
		goto err_ar_pci;
	}

	ar_pci->ar = ar;
	ar_pci->fw_indicator_address = FW_INDICATOR_ADDRESS;
	atomic_set(&ar_pci->keep_awake_count, 0);

	pci_set_drvdata(pdev, ar);

	/*
	 * Without any knowledge of the Host, the Target may have been reset or
	 * power cycled and its Config Space may no longer reflect the PCI
	 * address space that was assigned earlier by the PCI infrastructure.
	 * Refresh it now.
	 */
	ret = pci_assign_resource(pdev, BAR_NUM);
	if (ret) {
		ath10k_err("cannot assign PCI space: %d\n", ret);
		goto err_ar;
	}

	ret = pci_enable_device(pdev);
	if (ret) {
		ath10k_err("cannot enable PCI device: %d\n", ret);
		goto err_ar;
	}

	/* Request MMIO resources */
	ret = pci_request_region(pdev, BAR_NUM, "ath");
	if (ret) {
		ath10k_err("PCI MMIO reservation error: %d\n", ret);
		goto err_device;
	}

	/*
	 * Target structures have a limit of 32 bit DMA pointers.
	 * DMA pointers can be wider than 32 bits by default on some systems.
	 */
	ret = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
	if (ret) {
		ath10k_err("32-bit DMA not available: %d\n", ret);
		goto err_region;
	}

	ret = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32));
	if (ret) {
		ath10k_err("cannot enable 32-bit consistent DMA\n");
		goto err_region;
	}

	/* Set bus master bit in PCI_COMMAND to enable DMA */
	pci_set_master(pdev);

	/*
	 * Temporary FIX: disable ASPM
	 * Will be removed after the OTP is programmed
	 */
	pci_read_config_dword(pdev, 0x80, &lcr_val);
	pci_write_config_dword(pdev, 0x80, (lcr_val & 0xffffff00));

	/* Arrange for access to Target SoC registers. */
	mem = pci_iomap(pdev, BAR_NUM, 0);
	if (!mem) {
		ath10k_err("PCI iomap error\n");
		ret = -EIO;
		goto err_master;
	}

	ar_pci->mem = mem;

	spin_lock_init(&ar_pci->ce_lock);

	ret = ath10k_do_pci_wake(ar);
	if (ret) {
		ath10k_err("Failed to get chip id: %d\n", ret);
		return ret;
	}

	chip_id = ath10k_pci_read32(ar,
				    RTC_SOC_BASE_ADDRESS + SOC_CHIP_ID_ADDRESS);

	ath10k_do_pci_sleep(ar);

	ret = ath10k_core_register(ar, chip_id);
	if (ret) {
		ath10k_err("could not register driver core (%d)\n", ret);
		goto err_iomap;
	}

	return 0;

err_iomap:
	pci_iounmap(pdev, mem);
err_master:
	pci_clear_master(pdev);
err_region:
	pci_release_region(pdev, BAR_NUM);
err_device:
	pci_disable_device(pdev);
err_ar:
	pci_set_drvdata(pdev, NULL);
	ath10k_core_destroy(ar);
err_ar_pci:
	/* call HIF PCI free here */
	kfree(ar_pci);

	return ret;
}

static void ath10k_pci_remove(struct pci_dev *pdev)
{
	struct ath10k *ar = pci_get_drvdata(pdev);
	struct ath10k_pci *ar_pci;

	ath10k_dbg(ATH10K_DBG_PCI, "%s\n", __func__);

	if (!ar)
		return;

	ar_pci = ath10k_pci_priv(ar);

	if (!ar_pci)
		return;

	tasklet_kill(&ar_pci->msi_fw_err);

	ath10k_core_unregister(ar);

	pci_set_drvdata(pdev, NULL);
	pci_iounmap(pdev, ar_pci->mem);
	pci_release_region(pdev, BAR_NUM);
	pci_clear_master(pdev);
	pci_disable_device(pdev);

	ath10k_core_destroy(ar);
	kfree(ar_pci);
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
		ath10k_err("pci_register_driver failed [%d]\n", ret);

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
MODULE_FIRMWARE(QCA988X_HW_2_0_FW_DIR "/" QCA988X_HW_2_0_FW_FILE);
MODULE_FIRMWARE(QCA988X_HW_2_0_FW_DIR "/" QCA988X_HW_2_0_OTP_FILE);
MODULE_FIRMWARE(QCA988X_HW_2_0_FW_DIR "/" QCA988X_HW_2_0_BOARD_DATA_FILE);
