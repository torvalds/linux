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

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include "wmi.h"
#include "wil6210.h"
#include "txrx.h"
#include "pmc.h"

struct desc_alloc_info {
	dma_addr_t pa;
	void	  *va;
};

static int wil_is_pmc_allocated(struct pmc_ctx *pmc)
{
	return !!pmc->pring_va;
}

void wil_pmc_init(struct wil6210_priv *wil)
{
	memset(&wil->pmc, 0, sizeof(struct pmc_ctx));
	mutex_init(&wil->pmc.lock);
}

/**
 * Allocate the physical ring (p-ring) and the required
 * number of descriptors of required size.
 * Initialize the descriptors as required by pmc dma.
 * The descriptors' buffers dwords are initialized to hold
 * dword's serial number in the lsw and reserved value
 * PCM_DATA_INVALID_DW_VAL in the msw.
 */
void wil_pmc_alloc(struct wil6210_priv *wil,
		   int num_descriptors,
		   int descriptor_size)
{
	u32 i;
	struct pmc_ctx *pmc = &wil->pmc;
	struct device *dev = wil_to_dev(wil);
	struct wmi_pmc_cmd pmc_cmd = {0};
	int last_cmd_err = -ENOMEM;

	mutex_lock(&pmc->lock);

	if (wil_is_pmc_allocated(pmc)) {
		/* sanity check */
		wil_err(wil, "%s: ERROR pmc is already allocated\n", __func__);
		goto no_release_err;
	}
	if ((num_descriptors <= 0) || (descriptor_size <= 0)) {
		wil_err(wil,
			"Invalid params num_descriptors(%d), descriptor_size(%d)\n",
			num_descriptors, descriptor_size);
		last_cmd_err = -EINVAL;
		goto no_release_err;
	}

	if (num_descriptors > (1 << WIL_RING_SIZE_ORDER_MAX)) {
		wil_err(wil,
			"num_descriptors(%d) exceeds max ring size %d\n",
			num_descriptors, 1 << WIL_RING_SIZE_ORDER_MAX);
		last_cmd_err = -EINVAL;
		goto no_release_err;
	}

	if (num_descriptors > INT_MAX / descriptor_size) {
		wil_err(wil,
			"Overflow in num_descriptors(%d)*descriptor_size(%d)\n",
			num_descriptors, descriptor_size);
		last_cmd_err = -EINVAL;
		goto no_release_err;
	}

	pmc->num_descriptors = num_descriptors;
	pmc->descriptor_size = descriptor_size;

	wil_dbg_misc(wil, "%s: %d descriptors x %d bytes each\n",
		     __func__, num_descriptors, descriptor_size);

	/* allocate descriptors info list in pmc context*/
	pmc->descriptors = kcalloc(num_descriptors,
				  sizeof(struct desc_alloc_info),
				  GFP_KERNEL);
	if (!pmc->descriptors) {
		wil_err(wil, "%s: ERROR allocating pmc skb list\n", __func__);
		goto no_release_err;
	}

	wil_dbg_misc(wil,
		     "%s: allocated descriptors info list %p\n",
		     __func__, pmc->descriptors);

	/* Allocate pring buffer and descriptors.
	 * vring->va should be aligned on its size rounded up to power of 2
	 * This is granted by the dma_alloc_coherent
	 */
	pmc->pring_va = dma_alloc_coherent(dev,
			sizeof(struct vring_tx_desc) * num_descriptors,
			&pmc->pring_pa,
			GFP_KERNEL);

	wil_dbg_misc(wil,
		     "%s: allocated pring %p => %pad. %zd x %d = total %zd bytes\n",
		     __func__,
		     pmc->pring_va, &pmc->pring_pa,
		     sizeof(struct vring_tx_desc),
		     num_descriptors,
		     sizeof(struct vring_tx_desc) * num_descriptors);

	if (!pmc->pring_va) {
		wil_err(wil, "%s: ERROR allocating pmc pring\n", __func__);
		goto release_pmc_skb_list;
	}

	/* initially, all descriptors are SW owned
	 * For Tx, Rx, and PMC, ownership bit is at the same location, thus
	 * we can use any
	 */
	for (i = 0; i < num_descriptors; i++) {
		struct vring_tx_desc *_d = &pmc->pring_va[i];
		struct vring_tx_desc dd = {}, *d = &dd;
		int j = 0;

		pmc->descriptors[i].va = dma_alloc_coherent(dev,
			descriptor_size,
			&pmc->descriptors[i].pa,
			GFP_KERNEL);

		if (unlikely(!pmc->descriptors[i].va)) {
			wil_err(wil,
				"%s: ERROR allocating pmc descriptor %d",
				__func__, i);
			goto release_pmc_skbs;
		}

		for (j = 0; j < descriptor_size / sizeof(u32); j++) {
			u32 *p = (u32 *)pmc->descriptors[i].va + j;
			*p = PCM_DATA_INVALID_DW_VAL | j;
		}

		/* configure dma descriptor */
		d->dma.addr.addr_low =
			cpu_to_le32(lower_32_bits(pmc->descriptors[i].pa));
		d->dma.addr.addr_high =
			cpu_to_le16((u16)upper_32_bits(pmc->descriptors[i].pa));
		d->dma.status = 0; /* 0 = HW_OWNED */
		d->dma.length = cpu_to_le16(descriptor_size);
		d->dma.d0 = BIT(9) | RX_DMA_D0_CMD_DMA_IT;
		*_d = *d;
	}

	wil_dbg_misc(wil, "%s: allocated successfully\n", __func__);

	pmc_cmd.op = WMI_PMC_ALLOCATE;
	pmc_cmd.ring_size = cpu_to_le16(pmc->num_descriptors);
	pmc_cmd.mem_base = cpu_to_le64(pmc->pring_pa);

	wil_dbg_misc(wil, "%s: send WMI_PMC_CMD with ALLOCATE op\n", __func__);
	pmc->last_cmd_status = wmi_send(wil,
					WMI_PMC_CMDID,
					&pmc_cmd,
					sizeof(pmc_cmd));
	if (pmc->last_cmd_status) {
		wil_err(wil,
			"%s: WMI_PMC_CMD with ALLOCATE op failed with status %d",
			__func__, pmc->last_cmd_status);
		goto release_pmc_skbs;
	}

	mutex_unlock(&pmc->lock);

	return;

release_pmc_skbs:
	wil_err(wil, "%s: exit on error: Releasing skbs...\n", __func__);
	for (i = 0; pmc->descriptors[i].va && i < num_descriptors; i++) {
		dma_free_coherent(dev,
				  descriptor_size,
				  pmc->descriptors[i].va,
				  pmc->descriptors[i].pa);

		pmc->descriptors[i].va = NULL;
	}
	wil_err(wil, "%s: exit on error: Releasing pring...\n", __func__);

	dma_free_coherent(dev,
			  sizeof(struct vring_tx_desc) * num_descriptors,
			  pmc->pring_va,
			  pmc->pring_pa);

	pmc->pring_va = NULL;

release_pmc_skb_list:
	wil_err(wil, "%s: exit on error: Releasing descriptors info list...\n",
		__func__);
	kfree(pmc->descriptors);
	pmc->descriptors = NULL;

no_release_err:
	pmc->last_cmd_status = last_cmd_err;
	mutex_unlock(&pmc->lock);
}

/**
 * Traverse the p-ring and release all buffers.
 * At the end release the p-ring memory
 */
void wil_pmc_free(struct wil6210_priv *wil, int send_pmc_cmd)
{
	struct pmc_ctx *pmc = &wil->pmc;
	struct device *dev = wil_to_dev(wil);
	struct wmi_pmc_cmd pmc_cmd = {0};

	mutex_lock(&pmc->lock);

	pmc->last_cmd_status = 0;

	if (!wil_is_pmc_allocated(pmc)) {
		wil_dbg_misc(wil, "%s: Error, can't free - not allocated\n",
			     __func__);
		pmc->last_cmd_status = -EPERM;
		mutex_unlock(&pmc->lock);
		return;
	}

	if (send_pmc_cmd) {
		wil_dbg_misc(wil, "%s: send WMI_PMC_CMD with RELEASE op\n",
			     __func__);
		pmc_cmd.op = WMI_PMC_RELEASE;
		pmc->last_cmd_status =
				wmi_send(wil, WMI_PMC_CMDID, &pmc_cmd,
					 sizeof(pmc_cmd));
		if (pmc->last_cmd_status) {
			wil_err(wil,
				"%s WMI_PMC_CMD with RELEASE op failed, status %d",
				__func__, pmc->last_cmd_status);
			/* There's nothing we can do with this error.
			 * Normally, it should never occur.
			 * Continue to freeing all memory allocated for pmc.
			 */
		}
	}

	if (pmc->pring_va) {
		size_t buf_size = sizeof(struct vring_tx_desc) *
				  pmc->num_descriptors;

		wil_dbg_misc(wil, "%s: free pring va %p\n",
			     __func__, pmc->pring_va);
		dma_free_coherent(dev, buf_size, pmc->pring_va, pmc->pring_pa);

		pmc->pring_va = NULL;
	} else {
		pmc->last_cmd_status = -ENOENT;
	}

	if (pmc->descriptors) {
		int i;

		for (i = 0;
		     pmc->descriptors[i].va && i < pmc->num_descriptors; i++) {
			dma_free_coherent(dev,
					  pmc->descriptor_size,
					  pmc->descriptors[i].va,
					  pmc->descriptors[i].pa);
			pmc->descriptors[i].va = NULL;
		}
		wil_dbg_misc(wil, "%s: free descriptor info %d/%d\n",
			     __func__, i, pmc->num_descriptors);
		wil_dbg_misc(wil,
			     "%s: free pmc descriptors info list %p\n",
			     __func__, pmc->descriptors);
		kfree(pmc->descriptors);
		pmc->descriptors = NULL;
	} else {
		pmc->last_cmd_status = -ENOENT;
	}

	mutex_unlock(&pmc->lock);
}

/**
 * Status of the last operation requested via debugfs: alloc/free/read.
 * 0 - success or negative errno
 */
int wil_pmc_last_cmd_status(struct wil6210_priv *wil)
{
	wil_dbg_misc(wil, "%s: status %d\n", __func__,
		     wil->pmc.last_cmd_status);

	return wil->pmc.last_cmd_status;
}

/**
 * Read from required position up to the end of current descriptor,
 * depends on descriptor size configured during alloc request.
 */
ssize_t wil_pmc_read(struct file *filp, char __user *buf, size_t count,
		     loff_t *f_pos)
{
	struct wil6210_priv *wil = filp->private_data;
	struct pmc_ctx *pmc = &wil->pmc;
	size_t retval = 0;
	unsigned long long idx;
	loff_t offset;
	size_t pmc_size;

	mutex_lock(&pmc->lock);

	if (!wil_is_pmc_allocated(pmc)) {
		wil_err(wil, "%s: error, pmc is not allocated!\n", __func__);
		pmc->last_cmd_status = -EPERM;
		mutex_unlock(&pmc->lock);
		return -EPERM;
	}

	pmc_size = pmc->descriptor_size * pmc->num_descriptors;

	wil_dbg_misc(wil,
		     "%s: size %u, pos %lld\n",
		     __func__, (unsigned)count, *f_pos);

	pmc->last_cmd_status = 0;

	idx = *f_pos;
	do_div(idx, pmc->descriptor_size);
	offset = *f_pos - (idx * pmc->descriptor_size);

	if (*f_pos >= pmc_size) {
		wil_dbg_misc(wil, "%s: reached end of pmc buf: %lld >= %u\n",
			     __func__, *f_pos, (unsigned)pmc_size);
		pmc->last_cmd_status = -ERANGE;
		goto out;
	}

	wil_dbg_misc(wil,
		     "%s: read from pos %lld (descriptor %llu, offset %llu) %zu bytes\n",
		     __func__, *f_pos, idx, offset, count);

	/* if no errors, return the copied byte count */
	retval = simple_read_from_buffer(buf,
					 count,
					 &offset,
					 pmc->descriptors[idx].va,
					 pmc->descriptor_size);
	*f_pos += retval;
out:
	mutex_unlock(&pmc->lock);

	return retval;
}

loff_t wil_pmc_llseek(struct file *filp, loff_t off, int whence)
{
	loff_t newpos;
	struct wil6210_priv *wil = filp->private_data;
	struct pmc_ctx *pmc = &wil->pmc;
	size_t pmc_size;

	mutex_lock(&pmc->lock);

	if (!wil_is_pmc_allocated(pmc)) {
		wil_err(wil, "error, pmc is not allocated!\n");
		pmc->last_cmd_status = -EPERM;
		mutex_unlock(&pmc->lock);
		return -EPERM;
	}

	pmc_size = pmc->descriptor_size * pmc->num_descriptors;

	switch (whence) {
	case 0: /* SEEK_SET */
		newpos = off;
		break;

	case 1: /* SEEK_CUR */
		newpos = filp->f_pos + off;
		break;

	case 2: /* SEEK_END */
		newpos = pmc_size;
		break;

	default: /* can't happen */
		newpos = -EINVAL;
		goto out;
	}

	if (newpos < 0) {
		newpos = -EINVAL;
		goto out;
	}
	if (newpos > pmc_size)
		newpos = pmc_size;

	filp->f_pos = newpos;

out:
	mutex_unlock(&pmc->lock);

	return newpos;
}
