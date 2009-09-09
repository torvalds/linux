/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2004 - 2009 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * BSD LICENSE
 *
 * Copyright(c) 2004-2009 Intel Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Support routines for v3+ hardware
 */

#include <linux/pci.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include "registers.h"
#include "hw.h"
#include "dma.h"
#include "dma_v2.h"

/* ioat hardware assumes at least two sources for raid operations */
#define src_cnt_to_sw(x) ((x) + 2)
#define src_cnt_to_hw(x) ((x) - 2)

/* provide a lookup table for setting the source address in the base or
 * extended descriptor of an xor descriptor
 */
static const u8 xor_idx_to_desc __read_mostly = 0xd0;
static const u8 xor_idx_to_field[] __read_mostly = { 1, 4, 5, 6, 7, 0, 1, 2 };

static dma_addr_t xor_get_src(struct ioat_raw_descriptor *descs[2], int idx)
{
	struct ioat_raw_descriptor *raw = descs[xor_idx_to_desc >> idx & 1];

	return raw->field[xor_idx_to_field[idx]];
}

static void xor_set_src(struct ioat_raw_descriptor *descs[2],
			dma_addr_t addr, u32 offset, int idx)
{
	struct ioat_raw_descriptor *raw = descs[xor_idx_to_desc >> idx & 1];

	raw->field[xor_idx_to_field[idx]] = addr + offset;
}

static void ioat3_dma_unmap(struct ioat2_dma_chan *ioat,
			    struct ioat_ring_ent *desc, int idx)
{
	struct ioat_chan_common *chan = &ioat->base;
	struct pci_dev *pdev = chan->device->pdev;
	size_t len = desc->len;
	size_t offset = len - desc->hw->size;
	struct dma_async_tx_descriptor *tx = &desc->txd;
	enum dma_ctrl_flags flags = tx->flags;

	switch (desc->hw->ctl_f.op) {
	case IOAT_OP_COPY:
		ioat_dma_unmap(chan, flags, len, desc->hw);
		break;
	case IOAT_OP_FILL: {
		struct ioat_fill_descriptor *hw = desc->fill;

		if (!(flags & DMA_COMPL_SKIP_DEST_UNMAP))
			ioat_unmap(pdev, hw->dst_addr - offset, len,
				   PCI_DMA_FROMDEVICE, flags, 1);
		break;
	}
	case IOAT_OP_XOR_VAL:
	case IOAT_OP_XOR: {
		struct ioat_xor_descriptor *xor = desc->xor;
		struct ioat_ring_ent *ext;
		struct ioat_xor_ext_descriptor *xor_ex = NULL;
		int src_cnt = src_cnt_to_sw(xor->ctl_f.src_cnt);
		struct ioat_raw_descriptor *descs[2];
		int i;

		if (src_cnt > 5) {
			ext = ioat2_get_ring_ent(ioat, idx + 1);
			xor_ex = ext->xor_ex;
		}

		if (!(flags & DMA_COMPL_SKIP_SRC_UNMAP)) {
			descs[0] = (struct ioat_raw_descriptor *) xor;
			descs[1] = (struct ioat_raw_descriptor *) xor_ex;
			for (i = 0; i < src_cnt; i++) {
				dma_addr_t src = xor_get_src(descs, i);

				ioat_unmap(pdev, src - offset, len,
					   PCI_DMA_TODEVICE, flags, 0);
			}

			/* dest is a source in xor validate operations */
			if (xor->ctl_f.op == IOAT_OP_XOR_VAL) {
				ioat_unmap(pdev, xor->dst_addr - offset, len,
					   PCI_DMA_TODEVICE, flags, 1);
				break;
			}
		}

		if (!(flags & DMA_COMPL_SKIP_DEST_UNMAP))
			ioat_unmap(pdev, xor->dst_addr - offset, len,
				   PCI_DMA_FROMDEVICE, flags, 1);
		break;
	}
	default:
		dev_err(&pdev->dev, "%s: unknown op type: %#x\n",
			__func__, desc->hw->ctl_f.op);
	}
}

static bool desc_has_ext(struct ioat_ring_ent *desc)
{
	struct ioat_dma_descriptor *hw = desc->hw;

	if (hw->ctl_f.op == IOAT_OP_XOR ||
	    hw->ctl_f.op == IOAT_OP_XOR_VAL) {
		struct ioat_xor_descriptor *xor = desc->xor;

		if (src_cnt_to_sw(xor->ctl_f.src_cnt) > 5)
			return true;
	}

	return false;
}

/**
 * __cleanup - reclaim used descriptors
 * @ioat: channel (ring) to clean
 *
 * The difference from the dma_v2.c __cleanup() is that this routine
 * handles extended descriptors and dma-unmapping raid operations.
 */
static void __cleanup(struct ioat2_dma_chan *ioat, unsigned long phys_complete)
{
	struct ioat_chan_common *chan = &ioat->base;
	struct ioat_ring_ent *desc;
	bool seen_current = false;
	u16 active;
	int i;

	dev_dbg(to_dev(chan), "%s: head: %#x tail: %#x issued: %#x\n",
		__func__, ioat->head, ioat->tail, ioat->issued);

	active = ioat2_ring_active(ioat);
	for (i = 0; i < active && !seen_current; i++) {
		struct dma_async_tx_descriptor *tx;

		prefetch(ioat2_get_ring_ent(ioat, ioat->tail + i + 1));
		desc = ioat2_get_ring_ent(ioat, ioat->tail + i);
		dump_desc_dbg(ioat, desc);
		tx = &desc->txd;
		if (tx->cookie) {
			chan->completed_cookie = tx->cookie;
			ioat3_dma_unmap(ioat, desc, ioat->tail + i);
			tx->cookie = 0;
			if (tx->callback) {
				tx->callback(tx->callback_param);
				tx->callback = NULL;
			}
		}

		if (tx->phys == phys_complete)
			seen_current = true;

		/* skip extended descriptors */
		if (desc_has_ext(desc)) {
			BUG_ON(i + 1 >= active);
			i++;
		}
	}
	ioat->tail += i;
	BUG_ON(!seen_current); /* no active descs have written a completion? */
	chan->last_completion = phys_complete;
	if (ioat->head == ioat->tail) {
		dev_dbg(to_dev(chan), "%s: cancel completion timeout\n",
			__func__);
		clear_bit(IOAT_COMPLETION_PENDING, &chan->state);
		mod_timer(&chan->timer, jiffies + IDLE_TIMEOUT);
	}
}

static void ioat3_cleanup(struct ioat2_dma_chan *ioat)
{
	struct ioat_chan_common *chan = &ioat->base;
	unsigned long phys_complete;

	prefetch(chan->completion);

	if (!spin_trylock_bh(&chan->cleanup_lock))
		return;

	if (!ioat_cleanup_preamble(chan, &phys_complete)) {
		spin_unlock_bh(&chan->cleanup_lock);
		return;
	}

	if (!spin_trylock_bh(&ioat->ring_lock)) {
		spin_unlock_bh(&chan->cleanup_lock);
		return;
	}

	__cleanup(ioat, phys_complete);

	spin_unlock_bh(&ioat->ring_lock);
	spin_unlock_bh(&chan->cleanup_lock);
}

static void ioat3_cleanup_tasklet(unsigned long data)
{
	struct ioat2_dma_chan *ioat = (void *) data;

	ioat3_cleanup(ioat);
	writew(IOAT_CHANCTRL_RUN | IOAT3_CHANCTRL_COMPL_DCA_EN,
	       ioat->base.reg_base + IOAT_CHANCTRL_OFFSET);
}

static void ioat3_restart_channel(struct ioat2_dma_chan *ioat)
{
	struct ioat_chan_common *chan = &ioat->base;
	unsigned long phys_complete;
	u32 status;

	status = ioat_chansts(chan);
	if (is_ioat_active(status) || is_ioat_idle(status))
		ioat_suspend(chan);
	while (is_ioat_active(status) || is_ioat_idle(status)) {
		status = ioat_chansts(chan);
		cpu_relax();
	}

	if (ioat_cleanup_preamble(chan, &phys_complete))
		__cleanup(ioat, phys_complete);

	__ioat2_restart_chan(ioat);
}

static void ioat3_timer_event(unsigned long data)
{
	struct ioat2_dma_chan *ioat = (void *) data;
	struct ioat_chan_common *chan = &ioat->base;

	spin_lock_bh(&chan->cleanup_lock);
	if (test_bit(IOAT_COMPLETION_PENDING, &chan->state)) {
		unsigned long phys_complete;
		u64 status;

		spin_lock_bh(&ioat->ring_lock);
		status = ioat_chansts(chan);

		/* when halted due to errors check for channel
		 * programming errors before advancing the completion state
		 */
		if (is_ioat_halted(status)) {
			u32 chanerr;

			chanerr = readl(chan->reg_base + IOAT_CHANERR_OFFSET);
			BUG_ON(is_ioat_bug(chanerr));
		}

		/* if we haven't made progress and we have already
		 * acknowledged a pending completion once, then be more
		 * forceful with a restart
		 */
		if (ioat_cleanup_preamble(chan, &phys_complete))
			__cleanup(ioat, phys_complete);
		else if (test_bit(IOAT_COMPLETION_ACK, &chan->state))
			ioat3_restart_channel(ioat);
		else {
			set_bit(IOAT_COMPLETION_ACK, &chan->state);
			mod_timer(&chan->timer, jiffies + COMPLETION_TIMEOUT);
		}
		spin_unlock_bh(&ioat->ring_lock);
	} else {
		u16 active;

		/* if the ring is idle, empty, and oversized try to step
		 * down the size
		 */
		spin_lock_bh(&ioat->ring_lock);
		active = ioat2_ring_active(ioat);
		if (active == 0 && ioat->alloc_order > ioat_get_alloc_order())
			reshape_ring(ioat, ioat->alloc_order-1);
		spin_unlock_bh(&ioat->ring_lock);

		/* keep shrinking until we get back to our minimum
		 * default size
		 */
		if (ioat->alloc_order > ioat_get_alloc_order())
			mod_timer(&chan->timer, jiffies + IDLE_TIMEOUT);
	}
	spin_unlock_bh(&chan->cleanup_lock);
}

static enum dma_status
ioat3_is_complete(struct dma_chan *c, dma_cookie_t cookie,
		  dma_cookie_t *done, dma_cookie_t *used)
{
	struct ioat2_dma_chan *ioat = to_ioat2_chan(c);

	if (ioat_is_complete(c, cookie, done, used) == DMA_SUCCESS)
		return DMA_SUCCESS;

	ioat3_cleanup(ioat);

	return ioat_is_complete(c, cookie, done, used);
}

static struct dma_async_tx_descriptor *
ioat3_prep_memset_lock(struct dma_chan *c, dma_addr_t dest, int value,
		       size_t len, unsigned long flags)
{
	struct ioat2_dma_chan *ioat = to_ioat2_chan(c);
	struct ioat_ring_ent *desc;
	size_t total_len = len;
	struct ioat_fill_descriptor *fill;
	int num_descs;
	u64 src_data = (0x0101010101010101ULL) * (value & 0xff);
	u16 idx;
	int i;

	num_descs = ioat2_xferlen_to_descs(ioat, len);
	if (likely(num_descs) &&
	    ioat2_alloc_and_lock(&idx, ioat, num_descs) == 0)
		/* pass */;
	else
		return NULL;
	for (i = 0; i < num_descs; i++) {
		size_t xfer_size = min_t(size_t, len, 1 << ioat->xfercap_log);

		desc = ioat2_get_ring_ent(ioat, idx + i);
		fill = desc->fill;

		fill->size = xfer_size;
		fill->src_data = src_data;
		fill->dst_addr = dest;
		fill->ctl = 0;
		fill->ctl_f.op = IOAT_OP_FILL;

		len -= xfer_size;
		dest += xfer_size;
		dump_desc_dbg(ioat, desc);
	}

	desc->txd.flags = flags;
	desc->len = total_len;
	fill->ctl_f.int_en = !!(flags & DMA_PREP_INTERRUPT);
	fill->ctl_f.fence = !!(flags & DMA_PREP_FENCE);
	fill->ctl_f.compl_write = 1;
	dump_desc_dbg(ioat, desc);

	/* we leave the channel locked to ensure in order submission */
	return &desc->txd;
}

static struct dma_async_tx_descriptor *
__ioat3_prep_xor_lock(struct dma_chan *c, enum sum_check_flags *result,
		      dma_addr_t dest, dma_addr_t *src, unsigned int src_cnt,
		      size_t len, unsigned long flags)
{
	struct ioat2_dma_chan *ioat = to_ioat2_chan(c);
	struct ioat_ring_ent *compl_desc;
	struct ioat_ring_ent *desc;
	struct ioat_ring_ent *ext;
	size_t total_len = len;
	struct ioat_xor_descriptor *xor;
	struct ioat_xor_ext_descriptor *xor_ex = NULL;
	struct ioat_dma_descriptor *hw;
	u32 offset = 0;
	int num_descs;
	int with_ext;
	int i;
	u16 idx;
	u8 op = result ? IOAT_OP_XOR_VAL : IOAT_OP_XOR;

	BUG_ON(src_cnt < 2);

	num_descs = ioat2_xferlen_to_descs(ioat, len);
	/* we need 2x the number of descriptors to cover greater than 5
	 * sources
	 */
	if (src_cnt > 5) {
		with_ext = 1;
		num_descs *= 2;
	} else
		with_ext = 0;

	/* completion writes from the raid engine may pass completion
	 * writes from the legacy engine, so we need one extra null
	 * (legacy) descriptor to ensure all completion writes arrive in
	 * order.
	 */
	if (likely(num_descs) &&
	    ioat2_alloc_and_lock(&idx, ioat, num_descs+1) == 0)
		/* pass */;
	else
		return NULL;
	for (i = 0; i < num_descs; i += 1 + with_ext) {
		struct ioat_raw_descriptor *descs[2];
		size_t xfer_size = min_t(size_t, len, 1 << ioat->xfercap_log);
		int s;

		desc = ioat2_get_ring_ent(ioat, idx + i);
		xor = desc->xor;

		/* save a branch by unconditionally retrieving the
		 * extended descriptor xor_set_src() knows to not write
		 * to it in the single descriptor case
		 */
		ext = ioat2_get_ring_ent(ioat, idx + i + 1);
		xor_ex = ext->xor_ex;

		descs[0] = (struct ioat_raw_descriptor *) xor;
		descs[1] = (struct ioat_raw_descriptor *) xor_ex;
		for (s = 0; s < src_cnt; s++)
			xor_set_src(descs, src[s], offset, s);
		xor->size = xfer_size;
		xor->dst_addr = dest + offset;
		xor->ctl = 0;
		xor->ctl_f.op = op;
		xor->ctl_f.src_cnt = src_cnt_to_hw(src_cnt);

		len -= xfer_size;
		offset += xfer_size;
		dump_desc_dbg(ioat, desc);
	}

	/* last xor descriptor carries the unmap parameters and fence bit */
	desc->txd.flags = flags;
	desc->len = total_len;
	if (result)
		desc->result = result;
	xor->ctl_f.fence = !!(flags & DMA_PREP_FENCE);

	/* completion descriptor carries interrupt bit */
	compl_desc = ioat2_get_ring_ent(ioat, idx + i);
	compl_desc->txd.flags = flags & DMA_PREP_INTERRUPT;
	hw = compl_desc->hw;
	hw->ctl = 0;
	hw->ctl_f.null = 1;
	hw->ctl_f.int_en = !!(flags & DMA_PREP_INTERRUPT);
	hw->ctl_f.compl_write = 1;
	hw->size = NULL_DESC_BUFFER_SIZE;
	dump_desc_dbg(ioat, compl_desc);

	/* we leave the channel locked to ensure in order submission */
	return &desc->txd;
}

static struct dma_async_tx_descriptor *
ioat3_prep_xor(struct dma_chan *chan, dma_addr_t dest, dma_addr_t *src,
	       unsigned int src_cnt, size_t len, unsigned long flags)
{
	return __ioat3_prep_xor_lock(chan, NULL, dest, src, src_cnt, len, flags);
}

struct dma_async_tx_descriptor *
ioat3_prep_xor_val(struct dma_chan *chan, dma_addr_t *src,
		    unsigned int src_cnt, size_t len,
		    enum sum_check_flags *result, unsigned long flags)
{
	/* the cleanup routine only sets bits on validate failure, it
	 * does not clear bits on validate success... so clear it here
	 */
	*result = 0;

	return __ioat3_prep_xor_lock(chan, result, src[0], &src[1],
				     src_cnt - 1, len, flags);
}

int __devinit ioat3_dma_probe(struct ioatdma_device *device, int dca)
{
	struct pci_dev *pdev = device->pdev;
	struct dma_device *dma;
	struct dma_chan *c;
	struct ioat_chan_common *chan;
	int err;
	u16 dev_id;
	u32 cap;

	device->enumerate_channels = ioat2_enumerate_channels;
	device->cleanup_tasklet = ioat3_cleanup_tasklet;
	device->timer_fn = ioat3_timer_event;
	dma = &device->common;
	dma->device_prep_dma_memcpy = ioat2_dma_prep_memcpy_lock;
	dma->device_issue_pending = ioat2_issue_pending;
	dma->device_alloc_chan_resources = ioat2_alloc_chan_resources;
	dma->device_free_chan_resources = ioat2_free_chan_resources;
	dma->device_is_tx_complete = ioat3_is_complete;
	cap = readl(device->reg_base + IOAT_DMA_CAP_OFFSET);
	if (cap & IOAT_CAP_FILL_BLOCK) {
		dma_cap_set(DMA_MEMSET, dma->cap_mask);
		dma->device_prep_dma_memset = ioat3_prep_memset_lock;
	}
	if (cap & IOAT_CAP_XOR) {
		dma->max_xor = 8;
		dma->xor_align = 2;

		dma_cap_set(DMA_XOR, dma->cap_mask);
		dma->device_prep_dma_xor = ioat3_prep_xor;

		dma_cap_set(DMA_XOR_VAL, dma->cap_mask);
		dma->device_prep_dma_xor_val = ioat3_prep_xor_val;
	}

	/* -= IOAT ver.3 workarounds =- */
	/* Write CHANERRMSK_INT with 3E07h to mask out the errors
	 * that can cause stability issues for IOAT ver.3
	 */
	pci_write_config_dword(pdev, IOAT_PCI_CHANERRMASK_INT_OFFSET, 0x3e07);

	/* Clear DMAUNCERRSTS Cfg-Reg Parity Error status bit
	 * (workaround for spurious config parity error after restart)
	 */
	pci_read_config_word(pdev, IOAT_PCI_DEVICE_ID_OFFSET, &dev_id);
	if (dev_id == PCI_DEVICE_ID_INTEL_IOAT_TBG0)
		pci_write_config_dword(pdev, IOAT_PCI_DMAUNCERRSTS_OFFSET, 0x10);

	err = ioat_probe(device);
	if (err)
		return err;
	ioat_set_tcp_copy_break(262144);

	list_for_each_entry(c, &dma->channels, device_node) {
		chan = to_chan_common(c);
		writel(IOAT_DMA_DCA_ANY_CPU,
		       chan->reg_base + IOAT_DCACTRL_OFFSET);
	}

	err = ioat_register(device);
	if (err)
		return err;

	ioat_kobject_add(device, &ioat2_ktype);

	if (dca)
		device->dca = ioat3_dca_init(pdev, device->reg_base);

	return 0;
}
