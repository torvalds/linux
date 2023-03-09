// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2012-2017 Qualcomm Atheros, Inc.
 * Copyright (c) 2018-2019, The Linux Foundation. All rights reserved.
 */

#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/pci.h>
#include <linux/rtnetlink.h>
#include <linux/power_supply.h>
#include "wil6210.h"
#include "wmi.h"
#include "txrx.h"
#include "pmc.h"

/* Nasty hack. Better have per device instances */
static u32 mem_addr;
static u32 dbg_txdesc_index;
static u32 dbg_ring_index; /* 24+ for Rx, 0..23 for Tx */
static u32 dbg_status_msg_index;
/* 0..wil->num_rx_status_rings-1 for Rx, wil->tx_sring_idx for Tx */
static u32 dbg_sring_index;

enum dbg_off_type {
	doff_u32 = 0,
	doff_x32 = 1,
	doff_ulong = 2,
	doff_io32 = 3,
	doff_u8 = 4
};

/* offset to "wil" */
struct dbg_off {
	const char *name;
	umode_t mode;
	ulong off;
	enum dbg_off_type type;
};

static void wil_print_desc_edma(struct seq_file *s, struct wil6210_priv *wil,
				struct wil_ring *ring,
				char _s, char _h, int idx)
{
	u8 num_of_descs;
	bool has_skb = false;

	if (ring->is_rx) {
		struct wil_rx_enhanced_desc *rx_d =
			(struct wil_rx_enhanced_desc *)
			&ring->va[idx].rx.enhanced;
		u16 buff_id = le16_to_cpu(rx_d->mac.buff_id);

		if (wil->rx_buff_mgmt.buff_arr &&
		    wil_val_in_range(buff_id, 0, wil->rx_buff_mgmt.size))
			has_skb = wil->rx_buff_mgmt.buff_arr[buff_id].skb;
		seq_printf(s, "%c", (has_skb) ? _h : _s);
	} else {
		struct wil_tx_enhanced_desc *d =
			(struct wil_tx_enhanced_desc *)
			&ring->va[idx].tx.enhanced;

		num_of_descs = (u8)d->mac.d[2];
		has_skb = ring->ctx && ring->ctx[idx].skb;
		if (num_of_descs >= 1)
			seq_printf(s, "%c", has_skb ? _h : _s);
		else
			/* num_of_descs == 0, it's a frag in a list of descs */
			seq_printf(s, "%c", has_skb ? 'h' : _s);
	}
}

static void wil_print_ring(struct seq_file *s, struct wil6210_priv *wil,
			   const char *name, struct wil_ring *ring,
			   char _s, char _h)
{
	void __iomem *x;
	u32 v;

	seq_printf(s, "RING %s = {\n", name);
	seq_printf(s, "  pa     = %pad\n", &ring->pa);
	seq_printf(s, "  va     = 0x%p\n", ring->va);
	seq_printf(s, "  size   = %d\n", ring->size);
	if (wil->use_enhanced_dma_hw && ring->is_rx)
		seq_printf(s, "  swtail = %u\n", *ring->edma_rx_swtail.va);
	else
		seq_printf(s, "  swtail = %d\n", ring->swtail);
	seq_printf(s, "  swhead = %d\n", ring->swhead);
	if (wil->use_enhanced_dma_hw) {
		int ring_id = ring->is_rx ?
			WIL_RX_DESC_RING_ID : ring - wil->ring_tx;
		/* SUBQ_CONS is a table of 32 entries, one for each Q pair.
		 * lower 16bits are for even ring_id and upper 16bits are for
		 * odd ring_id
		 */
		x = wmi_addr(wil, RGF_DMA_SCM_SUBQ_CONS + 4 * (ring_id / 2));
		v = readl_relaxed(x);

		v = (ring_id % 2 ? (v >> 16) : (v & 0xffff));
		seq_printf(s, "  hwhead = %u\n", v);
	}
	seq_printf(s, "  hwtail = [0x%08x] -> ", ring->hwtail);
	x = wmi_addr(wil, ring->hwtail);
	if (x) {
		v = readl(x);
		seq_printf(s, "0x%08x = %d\n", v, v);
	} else {
		seq_puts(s, "???\n");
	}

	if (ring->va && (ring->size <= (1 << WIL_RING_SIZE_ORDER_MAX))) {
		uint i;

		for (i = 0; i < ring->size; i++) {
			if ((i % 128) == 0 && i != 0)
				seq_puts(s, "\n");
			if (wil->use_enhanced_dma_hw) {
				wil_print_desc_edma(s, wil, ring, _s, _h, i);
			} else {
				volatile struct vring_tx_desc *d =
					&ring->va[i].tx.legacy;
				seq_printf(s, "%c", (d->dma.status & BIT(0)) ?
					   _s : (ring->ctx[i].skb ? _h : 'h'));
			}
		}
		seq_puts(s, "\n");
	}
	seq_puts(s, "}\n");
}

static int ring_show(struct seq_file *s, void *data)
{
	uint i;
	struct wil6210_priv *wil = s->private;

	wil_print_ring(s, wil, "rx", &wil->ring_rx, 'S', '_');

	for (i = 0; i < ARRAY_SIZE(wil->ring_tx); i++) {
		struct wil_ring *ring = &wil->ring_tx[i];
		struct wil_ring_tx_data *txdata = &wil->ring_tx_data[i];

		if (ring->va) {
			int cid = wil->ring2cid_tid[i][0];
			int tid = wil->ring2cid_tid[i][1];
			u32 swhead = ring->swhead;
			u32 swtail = ring->swtail;
			int used = (ring->size + swhead - swtail)
				   % ring->size;
			int avail = ring->size - used - 1;
			char name[10];
			char sidle[10];
			/* performance monitoring */
			cycles_t now = get_cycles();
			uint64_t idle = txdata->idle * 100;
			uint64_t total = now - txdata->begin;

			if (total != 0) {
				do_div(idle, total);
				snprintf(sidle, sizeof(sidle), "%3d%%",
					 (int)idle);
			} else {
				snprintf(sidle, sizeof(sidle), "N/A");
			}
			txdata->begin = now;
			txdata->idle = 0ULL;

			snprintf(name, sizeof(name), "tx_%2d", i);

			if (cid < wil->max_assoc_sta)
				seq_printf(s,
					   "\n%pM CID %d TID %d 1x%s BACK([%u] %u TU A%s) [%3d|%3d] idle %s\n",
					   wil->sta[cid].addr, cid, tid,
					   txdata->dot1x_open ? "+" : "-",
					   txdata->agg_wsize,
					   txdata->agg_timeout,
					   txdata->agg_amsdu ? "+" : "-",
					   used, avail, sidle);
			else
				seq_printf(s,
					   "\nBroadcast 1x%s [%3d|%3d] idle %s\n",
					   txdata->dot1x_open ? "+" : "-",
					   used, avail, sidle);

			wil_print_ring(s, wil, name, ring, '_', 'H');
		}
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(ring);

static void wil_print_sring(struct seq_file *s, struct wil6210_priv *wil,
			    struct wil_status_ring *sring)
{
	void __iomem *x;
	int sring_idx = sring - wil->srings;
	u32 v;

	seq_printf(s, "Status Ring %s [ %d ] = {\n",
		   sring->is_rx ? "RX" : "TX", sring_idx);
	seq_printf(s, "  pa     = %pad\n", &sring->pa);
	seq_printf(s, "  va     = 0x%pK\n", sring->va);
	seq_printf(s, "  size   = %d\n", sring->size);
	seq_printf(s, "  elem_size   = %zu\n", sring->elem_size);
	seq_printf(s, "  swhead = %d\n", sring->swhead);
	if (wil->use_enhanced_dma_hw) {
		/* COMPQ_PROD is a table of 32 entries, one for each Q pair.
		 * lower 16bits are for even ring_id and upper 16bits are for
		 * odd ring_id
		 */
		x = wmi_addr(wil, RGF_DMA_SCM_COMPQ_PROD + 4 * (sring_idx / 2));
		v = readl_relaxed(x);

		v = (sring_idx % 2 ? (v >> 16) : (v & 0xffff));
		seq_printf(s, "  hwhead = %u\n", v);
	}
	seq_printf(s, "  hwtail = [0x%08x] -> ", sring->hwtail);
	x = wmi_addr(wil, sring->hwtail);
	if (x) {
		v = readl_relaxed(x);
		seq_printf(s, "0x%08x = %d\n", v, v);
	} else {
		seq_puts(s, "???\n");
	}
	seq_printf(s, "  desc_rdy_pol   = %d\n", sring->desc_rdy_pol);
	seq_printf(s, "  invalid_buff_id_cnt   = %d\n",
		   sring->invalid_buff_id_cnt);

	if (sring->va && (sring->size <= (1 << WIL_RING_SIZE_ORDER_MAX))) {
		uint i;

		for (i = 0; i < sring->size; i++) {
			u32 *sdword_0 =
				(u32 *)(sring->va + (sring->elem_size * i));

			if ((i % 128) == 0 && i != 0)
				seq_puts(s, "\n");
			if (i == sring->swhead)
				seq_printf(s, "%c", (*sdword_0 & BIT(31)) ?
					   'X' : 'x');
			else
				seq_printf(s, "%c", (*sdword_0 & BIT(31)) ?
					   '1' : '0');
		}
		seq_puts(s, "\n");
	}
	seq_puts(s, "}\n");
}

static int srings_show(struct seq_file *s, void *data)
{
	struct wil6210_priv *wil = s->private;
	int i = 0;

	for (i = 0; i < WIL6210_MAX_STATUS_RINGS; i++)
		if (wil->srings[i].va)
			wil_print_sring(s, wil, &wil->srings[i]);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(srings);

static void wil_seq_hexdump(struct seq_file *s, void *p, int len,
			    const char *prefix)
{
	seq_hex_dump(s, prefix, DUMP_PREFIX_NONE, 16, 1, p, len, false);
}

static void wil_print_mbox_ring(struct seq_file *s, const char *prefix,
				void __iomem *off)
{
	struct wil6210_priv *wil = s->private;
	struct wil6210_mbox_ring r;
	int rsize;
	uint i;

	wil_halp_vote(wil);

	if (wil_mem_access_lock(wil)) {
		wil_halp_unvote(wil);
		return;
	}

	wil_memcpy_fromio_32(&r, off, sizeof(r));
	wil_mbox_ring_le2cpus(&r);
	/*
	 * we just read memory block from NIC. This memory may be
	 * garbage. Check validity before using it.
	 */
	rsize = r.size / sizeof(struct wil6210_mbox_ring_desc);

	seq_printf(s, "ring %s = {\n", prefix);
	seq_printf(s, "  base = 0x%08x\n", r.base);
	seq_printf(s, "  size = 0x%04x bytes -> %d entries\n", r.size, rsize);
	seq_printf(s, "  tail = 0x%08x\n", r.tail);
	seq_printf(s, "  head = 0x%08x\n", r.head);
	seq_printf(s, "  entry size = %d\n", r.entry_size);

	if (r.size % sizeof(struct wil6210_mbox_ring_desc)) {
		seq_printf(s, "  ??? size is not multiple of %zd, garbage?\n",
			   sizeof(struct wil6210_mbox_ring_desc));
		goto out;
	}

	if (!wmi_addr(wil, r.base) ||
	    !wmi_addr(wil, r.tail) ||
	    !wmi_addr(wil, r.head)) {
		seq_puts(s, "  ??? pointers are garbage?\n");
		goto out;
	}

	for (i = 0; i < rsize; i++) {
		struct wil6210_mbox_ring_desc d;
		struct wil6210_mbox_hdr hdr;
		size_t delta = i * sizeof(d);
		void __iomem *x = wil->csr + HOSTADDR(r.base) + delta;

		wil_memcpy_fromio_32(&d, x, sizeof(d));

		seq_printf(s, "  [%2x] %s %s%s 0x%08x", i,
			   d.sync ? "F" : "E",
			   (r.tail - r.base == delta) ? "t" : " ",
			   (r.head - r.base == delta) ? "h" : " ",
			   le32_to_cpu(d.addr));
		if (0 == wmi_read_hdr(wil, d.addr, &hdr)) {
			u16 len = le16_to_cpu(hdr.len);

			seq_printf(s, " -> %04x %04x %04x %02x\n",
				   le16_to_cpu(hdr.seq), len,
				   le16_to_cpu(hdr.type), hdr.flags);
			if (len <= MAX_MBOXITEM_SIZE) {
				unsigned char databuf[MAX_MBOXITEM_SIZE];
				void __iomem *src = wmi_buffer(wil, d.addr) +
					sizeof(struct wil6210_mbox_hdr);
				/*
				 * No need to check @src for validity -
				 * we already validated @d.addr while
				 * reading header
				 */
				wil_memcpy_fromio_32(databuf, src, len);
				wil_seq_hexdump(s, databuf, len, "      : ");
			}
		} else {
			seq_puts(s, "\n");
		}
	}
 out:
	seq_puts(s, "}\n");
	wil_mem_access_unlock(wil);
	wil_halp_unvote(wil);
}

static int mbox_show(struct seq_file *s, void *data)
{
	struct wil6210_priv *wil = s->private;
	int ret;

	ret = wil_pm_runtime_get(wil);
	if (ret < 0)
		return ret;

	wil_print_mbox_ring(s, "tx", wil->csr + HOST_MBOX +
		       offsetof(struct wil6210_mbox_ctl, tx));
	wil_print_mbox_ring(s, "rx", wil->csr + HOST_MBOX +
		       offsetof(struct wil6210_mbox_ctl, rx));

	wil_pm_runtime_put(wil);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(mbox);

static int wil_debugfs_iomem_x32_set(void *data, u64 val)
{
	struct wil_debugfs_iomem_data *d = (struct
					    wil_debugfs_iomem_data *)data;
	struct wil6210_priv *wil = d->wil;
	int ret;

	ret = wil_pm_runtime_get(wil);
	if (ret < 0)
		return ret;

	writel_relaxed(val, (void __iomem *)d->offset);

	wmb(); /* make sure write propagated to HW */

	wil_pm_runtime_put(wil);

	return 0;
}

static int wil_debugfs_iomem_x32_get(void *data, u64 *val)
{
	struct wil_debugfs_iomem_data *d = (struct
					    wil_debugfs_iomem_data *)data;
	struct wil6210_priv *wil = d->wil;
	int ret;

	ret = wil_pm_runtime_get(wil);
	if (ret < 0)
		return ret;

	*val = readl((void __iomem *)d->offset);

	wil_pm_runtime_put(wil);

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_iomem_x32, wil_debugfs_iomem_x32_get,
			 wil_debugfs_iomem_x32_set, "0x%08llx\n");

static void wil_debugfs_create_iomem_x32(const char *name, umode_t mode,
					 struct dentry *parent, void *value,
					 struct wil6210_priv *wil)
{
	struct wil_debugfs_iomem_data *data = &wil->dbg_data.data_arr[
					      wil->dbg_data.iomem_data_count];

	data->wil = wil;
	data->offset = value;

	debugfs_create_file_unsafe(name, mode, parent, data, &fops_iomem_x32);
	wil->dbg_data.iomem_data_count++;
}

static int wil_debugfs_ulong_set(void *data, u64 val)
{
	*(ulong *)data = val;
	return 0;
}

static int wil_debugfs_ulong_get(void *data, u64 *val)
{
	*val = *(ulong *)data;
	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(wil_fops_ulong, wil_debugfs_ulong_get,
			 wil_debugfs_ulong_set, "0x%llx\n");

/**
 * wil6210_debugfs_init_offset - create set of debugfs files
 * @wil: driver's context, used for printing
 * @dbg: directory on the debugfs, where files will be created
 * @base: base address used in address calculation
 * @tbl: table with file descriptions. Should be terminated with empty element.
 *
 * Creates files accordingly to the @tbl.
 */
static void wil6210_debugfs_init_offset(struct wil6210_priv *wil,
					struct dentry *dbg, void *base,
					const struct dbg_off * const tbl)
{
	int i;

	for (i = 0; tbl[i].name; i++) {
		switch (tbl[i].type) {
		case doff_u32:
			debugfs_create_u32(tbl[i].name, tbl[i].mode, dbg,
					   base + tbl[i].off);
			break;
		case doff_x32:
			debugfs_create_x32(tbl[i].name, tbl[i].mode, dbg,
					   base + tbl[i].off);
			break;
		case doff_ulong:
			debugfs_create_file_unsafe(tbl[i].name, tbl[i].mode,
						   dbg, base + tbl[i].off,
						   &wil_fops_ulong);
			break;
		case doff_io32:
			wil_debugfs_create_iomem_x32(tbl[i].name, tbl[i].mode,
						     dbg, base + tbl[i].off,
						     wil);
			break;
		case doff_u8:
			debugfs_create_u8(tbl[i].name, tbl[i].mode, dbg,
					  base + tbl[i].off);
			break;
		}
	}
}

static const struct dbg_off isr_off[] = {
	{"ICC", 0644, offsetof(struct RGF_ICR, ICC), doff_io32},
	{"ICR", 0644, offsetof(struct RGF_ICR, ICR), doff_io32},
	{"ICM", 0644, offsetof(struct RGF_ICR, ICM), doff_io32},
	{"ICS",	0244, offsetof(struct RGF_ICR, ICS), doff_io32},
	{"IMV", 0644, offsetof(struct RGF_ICR, IMV), doff_io32},
	{"IMS",	0244, offsetof(struct RGF_ICR, IMS), doff_io32},
	{"IMC",	0244, offsetof(struct RGF_ICR, IMC), doff_io32},
	{},
};

static void wil6210_debugfs_create_ISR(struct wil6210_priv *wil,
				       const char *name, struct dentry *parent,
				       u32 off)
{
	struct dentry *d = debugfs_create_dir(name, parent);

	wil6210_debugfs_init_offset(wil, d, (void * __force)wil->csr + off,
				    isr_off);
}

static const struct dbg_off pseudo_isr_off[] = {
	{"CAUSE",   0444, HOSTADDR(RGF_DMA_PSEUDO_CAUSE), doff_io32},
	{"MASK_SW", 0444, HOSTADDR(RGF_DMA_PSEUDO_CAUSE_MASK_SW), doff_io32},
	{"MASK_FW", 0444, HOSTADDR(RGF_DMA_PSEUDO_CAUSE_MASK_FW), doff_io32},
	{},
};

static void wil6210_debugfs_create_pseudo_ISR(struct wil6210_priv *wil,
					      struct dentry *parent)
{
	struct dentry *d = debugfs_create_dir("PSEUDO_ISR", parent);

	wil6210_debugfs_init_offset(wil, d, (void * __force)wil->csr,
				    pseudo_isr_off);
}

static const struct dbg_off lgc_itr_cnt_off[] = {
	{"TRSH", 0644, HOSTADDR(RGF_DMA_ITR_CNT_TRSH), doff_io32},
	{"DATA", 0644, HOSTADDR(RGF_DMA_ITR_CNT_DATA), doff_io32},
	{"CTL",  0644, HOSTADDR(RGF_DMA_ITR_CNT_CRL), doff_io32},
	{},
};

static const struct dbg_off tx_itr_cnt_off[] = {
	{"TRSH", 0644, HOSTADDR(RGF_DMA_ITR_TX_CNT_TRSH),
	 doff_io32},
	{"DATA", 0644, HOSTADDR(RGF_DMA_ITR_TX_CNT_DATA),
	 doff_io32},
	{"CTL",  0644, HOSTADDR(RGF_DMA_ITR_TX_CNT_CTL),
	 doff_io32},
	{"IDL_TRSH", 0644, HOSTADDR(RGF_DMA_ITR_TX_IDL_CNT_TRSH),
	 doff_io32},
	{"IDL_DATA", 0644, HOSTADDR(RGF_DMA_ITR_TX_IDL_CNT_DATA),
	 doff_io32},
	{"IDL_CTL",  0644, HOSTADDR(RGF_DMA_ITR_TX_IDL_CNT_CTL),
	 doff_io32},
	{},
};

static const struct dbg_off rx_itr_cnt_off[] = {
	{"TRSH", 0644, HOSTADDR(RGF_DMA_ITR_RX_CNT_TRSH),
	 doff_io32},
	{"DATA", 0644, HOSTADDR(RGF_DMA_ITR_RX_CNT_DATA),
	 doff_io32},
	{"CTL",  0644, HOSTADDR(RGF_DMA_ITR_RX_CNT_CTL),
	 doff_io32},
	{"IDL_TRSH", 0644, HOSTADDR(RGF_DMA_ITR_RX_IDL_CNT_TRSH),
	 doff_io32},
	{"IDL_DATA", 0644, HOSTADDR(RGF_DMA_ITR_RX_IDL_CNT_DATA),
	 doff_io32},
	{"IDL_CTL",  0644, HOSTADDR(RGF_DMA_ITR_RX_IDL_CNT_CTL),
	 doff_io32},
	{},
};

static int wil6210_debugfs_create_ITR_CNT(struct wil6210_priv *wil,
					  struct dentry *parent)
{
	struct dentry *d, *dtx, *drx;

	d = debugfs_create_dir("ITR_CNT", parent);

	dtx = debugfs_create_dir("TX", d);
	drx = debugfs_create_dir("RX", d);

	wil6210_debugfs_init_offset(wil, d, (void * __force)wil->csr,
				    lgc_itr_cnt_off);

	wil6210_debugfs_init_offset(wil, dtx, (void * __force)wil->csr,
				    tx_itr_cnt_off);

	wil6210_debugfs_init_offset(wil, drx, (void * __force)wil->csr,
				    rx_itr_cnt_off);
	return 0;
}

static int memread_show(struct seq_file *s, void *data)
{
	struct wil6210_priv *wil = s->private;
	void __iomem *a;
	int ret;

	ret = wil_pm_runtime_get(wil);
	if (ret < 0)
		return ret;

	ret = wil_mem_access_lock(wil);
	if (ret) {
		wil_pm_runtime_put(wil);
		return ret;
	}

	a = wmi_buffer(wil, cpu_to_le32(mem_addr));

	if (a)
		seq_printf(s, "[0x%08x] = 0x%08x\n", mem_addr, readl(a));
	else
		seq_printf(s, "[0x%08x] = INVALID\n", mem_addr);

	wil_mem_access_unlock(wil);
	wil_pm_runtime_put(wil);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(memread);

static ssize_t wil_read_file_ioblob(struct file *file, char __user *user_buf,
				    size_t count, loff_t *ppos)
{
	enum { max_count = 4096 };
	struct wil_blob_wrapper *wil_blob = file->private_data;
	struct wil6210_priv *wil = wil_blob->wil;
	loff_t aligned_pos, pos = *ppos;
	size_t available = wil_blob->blob.size;
	void *buf;
	size_t unaligned_bytes, aligned_count, ret;
	int rc;

	if (pos < 0)
		return -EINVAL;

	if (pos >= available || !count)
		return 0;

	if (count > available - pos)
		count = available - pos;
	if (count > max_count)
		count = max_count;

	/* set pos to 4 bytes aligned */
	unaligned_bytes = pos % 4;
	aligned_pos = pos - unaligned_bytes;
	aligned_count = count + unaligned_bytes;

	buf = kmalloc(aligned_count, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	rc = wil_pm_runtime_get(wil);
	if (rc < 0) {
		kfree(buf);
		return rc;
	}

	rc = wil_mem_access_lock(wil);
	if (rc) {
		kfree(buf);
		wil_pm_runtime_put(wil);
		return rc;
	}

	wil_memcpy_fromio_32(buf, (const void __iomem *)
			     wil_blob->blob.data + aligned_pos, aligned_count);

	ret = copy_to_user(user_buf, buf + unaligned_bytes, count);

	wil_mem_access_unlock(wil);
	wil_pm_runtime_put(wil);

	kfree(buf);
	if (ret == count)
		return -EFAULT;

	count -= ret;
	*ppos = pos + count;

	return count;
}

static const struct file_operations fops_ioblob = {
	.read =		wil_read_file_ioblob,
	.open =		simple_open,
	.llseek =	default_llseek,
};

static
struct dentry *wil_debugfs_create_ioblob(const char *name,
					 umode_t mode,
					 struct dentry *parent,
					 struct wil_blob_wrapper *wil_blob)
{
	return debugfs_create_file(name, mode, parent, wil_blob, &fops_ioblob);
}

/*---write channel 1..4 to rxon for it, 0 to rxoff---*/
static ssize_t wil_write_file_rxon(struct file *file, const char __user *buf,
				   size_t len, loff_t *ppos)
{
	struct wil6210_priv *wil = file->private_data;
	int rc;
	long channel;
	bool on;

	char *kbuf = memdup_user_nul(buf, len);

	if (IS_ERR(kbuf))
		return PTR_ERR(kbuf);
	rc = kstrtol(kbuf, 0, &channel);
	kfree(kbuf);
	if (rc)
		return rc;

	if ((channel < 0) || (channel > 4)) {
		wil_err(wil, "Invalid channel %ld\n", channel);
		return -EINVAL;
	}
	on = !!channel;

	if (on) {
		rc = wmi_set_channel(wil, (int)channel);
		if (rc)
			return rc;
	}

	rc = wmi_rxon(wil, on);
	if (rc)
		return rc;

	return len;
}

static const struct file_operations fops_rxon = {
	.write = wil_write_file_rxon,
	.open  = simple_open,
};

static ssize_t wil_write_file_rbufcap(struct file *file,
				      const char __user *buf,
				      size_t count, loff_t *ppos)
{
	struct wil6210_priv *wil = file->private_data;
	int val;
	int rc;

	rc = kstrtoint_from_user(buf, count, 0, &val);
	if (rc) {
		wil_err(wil, "Invalid argument\n");
		return rc;
	}
	/* input value: negative to disable, 0 to use system default,
	 * 1..ring size to set descriptor threshold
	 */
	wil_info(wil, "%s RBUFCAP, descriptors threshold - %d\n",
		 val < 0 ? "Disabling" : "Enabling", val);

	if (!wil->ring_rx.va || val > wil->ring_rx.size) {
		wil_err(wil, "Invalid descriptors threshold, %d\n", val);
		return -EINVAL;
	}

	rc = wmi_rbufcap_cfg(wil, val < 0 ? 0 : 1, val < 0 ? 0 : val);
	if (rc) {
		wil_err(wil, "RBUFCAP config failed: %d\n", rc);
		return rc;
	}

	return count;
}

static const struct file_operations fops_rbufcap = {
	.write = wil_write_file_rbufcap,
	.open  = simple_open,
};

/* block ack control, write:
 * - "add <ringid> <agg_size> <timeout>" to trigger ADDBA
 * - "del_tx <ringid> <reason>" to trigger DELBA for Tx side
 * - "del_rx <CID> <TID> <reason>" to trigger DELBA for Rx side
 */
static ssize_t wil_write_back(struct file *file, const char __user *buf,
			      size_t len, loff_t *ppos)
{
	struct wil6210_priv *wil = file->private_data;
	int rc;
	char *kbuf = kmalloc(len + 1, GFP_KERNEL);
	char cmd[9];
	int p1, p2, p3;

	if (!kbuf)
		return -ENOMEM;

	rc = simple_write_to_buffer(kbuf, len, ppos, buf, len);
	if (rc != len) {
		kfree(kbuf);
		return rc >= 0 ? -EIO : rc;
	}

	kbuf[len] = '\0';
	rc = sscanf(kbuf, "%8s %d %d %d", cmd, &p1, &p2, &p3);
	kfree(kbuf);

	if (rc < 0)
		return rc;
	if (rc < 2)
		return -EINVAL;

	if ((strcmp(cmd, "add") == 0) ||
	    (strcmp(cmd, "del_tx") == 0)) {
		struct wil_ring_tx_data *txdata;

		if (p1 < 0 || p1 >= WIL6210_MAX_TX_RINGS) {
			wil_err(wil, "BACK: invalid ring id %d\n", p1);
			return -EINVAL;
		}
		txdata = &wil->ring_tx_data[p1];
		if (strcmp(cmd, "add") == 0) {
			if (rc < 3) {
				wil_err(wil, "BACK: add require at least 2 params\n");
				return -EINVAL;
			}
			if (rc < 4)
				p3 = 0;
			wmi_addba(wil, txdata->mid, p1, p2, p3);
		} else {
			if (rc < 3)
				p2 = WLAN_REASON_QSTA_LEAVE_QBSS;
			wmi_delba_tx(wil, txdata->mid, p1, p2);
		}
	} else if (strcmp(cmd, "del_rx") == 0) {
		struct wil_sta_info *sta;

		if (rc < 3) {
			wil_err(wil,
				"BACK: del_rx require at least 2 params\n");
			return -EINVAL;
		}
		if (p1 < 0 || p1 >= wil->max_assoc_sta) {
			wil_err(wil, "BACK: invalid CID %d\n", p1);
			return -EINVAL;
		}
		if (rc < 4)
			p3 = WLAN_REASON_QSTA_LEAVE_QBSS;
		sta = &wil->sta[p1];
		wmi_delba_rx(wil, sta->mid, p1, p2, p3);
	} else {
		wil_err(wil, "BACK: Unrecognized command \"%s\"\n", cmd);
		return -EINVAL;
	}

	return len;
}

static ssize_t wil_read_back(struct file *file, char __user *user_buf,
			     size_t count, loff_t *ppos)
{
	static const char text[] = "block ack control, write:\n"
	" - \"add <ringid> <agg_size> <timeout>\" to trigger ADDBA\n"
	"If missing, <timeout> defaults to 0\n"
	" - \"del_tx <ringid> <reason>\" to trigger DELBA for Tx side\n"
	" - \"del_rx <CID> <TID> <reason>\" to trigger DELBA for Rx side\n"
	"If missing, <reason> set to \"STA_LEAVING\" (36)\n";

	return simple_read_from_buffer(user_buf, count, ppos, text,
				       sizeof(text));
}

static const struct file_operations fops_back = {
	.read = wil_read_back,
	.write = wil_write_back,
	.open  = simple_open,
};

/* pmc control, write:
 * - "alloc <num descriptors> <descriptor_size>" to allocate PMC
 * - "free" to release memory allocated for PMC
 */
static ssize_t wil_write_pmccfg(struct file *file, const char __user *buf,
				size_t len, loff_t *ppos)
{
	struct wil6210_priv *wil = file->private_data;
	int rc;
	char *kbuf = kmalloc(len + 1, GFP_KERNEL);
	char cmd[9];
	int num_descs, desc_size;

	if (!kbuf)
		return -ENOMEM;

	rc = simple_write_to_buffer(kbuf, len, ppos, buf, len);
	if (rc != len) {
		kfree(kbuf);
		return rc >= 0 ? -EIO : rc;
	}

	kbuf[len] = '\0';
	rc = sscanf(kbuf, "%8s %d %d", cmd, &num_descs, &desc_size);
	kfree(kbuf);

	if (rc < 0)
		return rc;

	if (rc < 1) {
		wil_err(wil, "pmccfg: no params given\n");
		return -EINVAL;
	}

	if (0 == strcmp(cmd, "alloc")) {
		if (rc != 3) {
			wil_err(wil, "pmccfg: alloc requires 2 params\n");
			return -EINVAL;
		}
		wil_pmc_alloc(wil, num_descs, desc_size);
	} else if (0 == strcmp(cmd, "free")) {
		if (rc != 1) {
			wil_err(wil, "pmccfg: free does not have any params\n");
			return -EINVAL;
		}
		wil_pmc_free(wil, true);
	} else {
		wil_err(wil, "pmccfg: Unrecognized command \"%s\"\n", cmd);
		return -EINVAL;
	}

	return len;
}

static ssize_t wil_read_pmccfg(struct file *file, char __user *user_buf,
			       size_t count, loff_t *ppos)
{
	struct wil6210_priv *wil = file->private_data;
	char text[256];
	char help[] = "pmc control, write:\n"
	" - \"alloc <num descriptors> <descriptor_size>\" to allocate pmc\n"
	" - \"free\" to free memory allocated for pmc\n";

	snprintf(text, sizeof(text), "Last command status: %d\n\n%s",
		 wil_pmc_last_cmd_status(wil), help);

	return simple_read_from_buffer(user_buf, count, ppos, text,
				       strlen(text) + 1);
}

static const struct file_operations fops_pmccfg = {
	.read = wil_read_pmccfg,
	.write = wil_write_pmccfg,
	.open  = simple_open,
};

static const struct file_operations fops_pmcdata = {
	.open		= simple_open,
	.read		= wil_pmc_read,
	.llseek		= wil_pmc_llseek,
};

static int wil_pmcring_seq_open(struct inode *inode, struct file *file)
{
	return single_open(file, wil_pmcring_read, inode->i_private);
}

static const struct file_operations fops_pmcring = {
	.open		= wil_pmcring_seq_open,
	.release	= single_release,
	.read		= seq_read,
	.llseek		= seq_lseek,
};

/*---tx_mgmt---*/
/* Write mgmt frame to this file to send it */
static ssize_t wil_write_file_txmgmt(struct file *file, const char __user *buf,
				     size_t len, loff_t *ppos)
{
	struct wil6210_priv *wil = file->private_data;
	struct wiphy *wiphy = wil_to_wiphy(wil);
	struct wireless_dev *wdev = wil->main_ndev->ieee80211_ptr;
	struct cfg80211_mgmt_tx_params params;
	int rc;
	void *frame;

	memset(&params, 0, sizeof(params));

	if (!len)
		return -EINVAL;

	frame = memdup_user(buf, len);
	if (IS_ERR(frame))
		return PTR_ERR(frame);

	params.buf = frame;
	params.len = len;

	rc = wil_cfg80211_mgmt_tx(wiphy, wdev, &params, NULL);

	kfree(frame);
	wil_info(wil, "-> %d\n", rc);

	return len;
}

static const struct file_operations fops_txmgmt = {
	.write = wil_write_file_txmgmt,
	.open  = simple_open,
};

/* Write WMI command (w/o mbox header) to this file to send it
 * WMI starts from wil6210_mbox_hdr_wmi header
 */
static ssize_t wil_write_file_wmi(struct file *file, const char __user *buf,
				  size_t len, loff_t *ppos)
{
	struct wil6210_priv *wil = file->private_data;
	struct wil6210_vif *vif = ndev_to_vif(wil->main_ndev);
	struct wmi_cmd_hdr *wmi;
	void *cmd;
	int cmdlen = len - sizeof(struct wmi_cmd_hdr);
	u16 cmdid;
	int rc1;

	if (cmdlen < 0 || *ppos != 0)
		return -EINVAL;

	wmi = memdup_user(buf, len);
	if (IS_ERR(wmi))
		return PTR_ERR(wmi);

	cmd = (cmdlen > 0) ? &wmi[1] : NULL;
	cmdid = le16_to_cpu(wmi->command_id);

	rc1 = wmi_send(wil, cmdid, vif->mid, cmd, cmdlen);
	kfree(wmi);

	wil_info(wil, "0x%04x[%d] -> %d\n", cmdid, cmdlen, rc1);

	return len;
}

static const struct file_operations fops_wmi = {
	.write = wil_write_file_wmi,
	.open  = simple_open,
};

static void wil_seq_print_skb(struct seq_file *s, struct sk_buff *skb)
{
	int i = 0;
	int len = skb_headlen(skb);
	void *p = skb->data;
	int nr_frags = skb_shinfo(skb)->nr_frags;

	seq_printf(s, "    len = %d\n", len);
	wil_seq_hexdump(s, p, len, "      : ");

	if (nr_frags) {
		seq_printf(s, "    nr_frags = %d\n", nr_frags);
		for (i = 0; i < nr_frags; i++) {
			const skb_frag_t *frag = &skb_shinfo(skb)->frags[i];

			len = skb_frag_size(frag);
			p = skb_frag_address_safe(frag);
			seq_printf(s, "    [%2d] : len = %d\n", i, len);
			wil_seq_hexdump(s, p, len, "      : ");
		}
	}
}

/*---------Tx/Rx descriptor------------*/
static int txdesc_show(struct seq_file *s, void *data)
{
	struct wil6210_priv *wil = s->private;
	struct wil_ring *ring;
	bool tx;
	int ring_idx = dbg_ring_index;
	int txdesc_idx = dbg_txdesc_index;
	volatile struct vring_tx_desc *d;
	volatile u32 *u;
	struct sk_buff *skb;

	if (wil->use_enhanced_dma_hw) {
		/* RX ring index == 0 */
		if (ring_idx >= WIL6210_MAX_TX_RINGS) {
			seq_printf(s, "invalid ring index %d\n", ring_idx);
			return 0;
		}
		tx = ring_idx > 0; /* desc ring 0 is reserved for RX */
	} else {
		/* RX ring index == WIL6210_MAX_TX_RINGS */
		if (ring_idx > WIL6210_MAX_TX_RINGS) {
			seq_printf(s, "invalid ring index %d\n", ring_idx);
			return 0;
		}
		tx = (ring_idx < WIL6210_MAX_TX_RINGS);
	}

	ring = tx ? &wil->ring_tx[ring_idx] : &wil->ring_rx;

	if (!ring->va) {
		if (tx)
			seq_printf(s, "No Tx[%2d] RING\n", ring_idx);
		else
			seq_puts(s, "No Rx RING\n");
		return 0;
	}

	if (txdesc_idx >= ring->size) {
		if (tx)
			seq_printf(s, "[%2d] TxDesc index (%d) >= size (%d)\n",
				   ring_idx, txdesc_idx, ring->size);
		else
			seq_printf(s, "RxDesc index (%d) >= size (%d)\n",
				   txdesc_idx, ring->size);
		return 0;
	}

	/* use struct vring_tx_desc for Rx as well,
	 * only field used, .dma.length, is the same
	 */
	d = &ring->va[txdesc_idx].tx.legacy;
	u = (volatile u32 *)d;
	skb = NULL;

	if (wil->use_enhanced_dma_hw) {
		if (tx) {
			skb = ring->ctx ? ring->ctx[txdesc_idx].skb : NULL;
		} else if (wil->rx_buff_mgmt.buff_arr) {
			struct wil_rx_enhanced_desc *rx_d =
				(struct wil_rx_enhanced_desc *)
				&ring->va[txdesc_idx].rx.enhanced;
			u16 buff_id = le16_to_cpu(rx_d->mac.buff_id);

			if (!wil_val_in_range(buff_id, 0,
					      wil->rx_buff_mgmt.size))
				seq_printf(s, "invalid buff_id %d\n", buff_id);
			else
				skb = wil->rx_buff_mgmt.buff_arr[buff_id].skb;
		}
	} else {
		skb = ring->ctx[txdesc_idx].skb;
	}
	if (tx)
		seq_printf(s, "Tx[%2d][%3d] = {\n", ring_idx,
			   txdesc_idx);
	else
		seq_printf(s, "Rx[%3d] = {\n", txdesc_idx);
	seq_printf(s, "  MAC = 0x%08x 0x%08x 0x%08x 0x%08x\n",
		   u[0], u[1], u[2], u[3]);
	seq_printf(s, "  DMA = 0x%08x 0x%08x 0x%08x 0x%08x\n",
		   u[4], u[5], u[6], u[7]);
	seq_printf(s, "  SKB = 0x%p\n", skb);

	if (skb) {
		skb_get(skb);
		wil_seq_print_skb(s, skb);
		kfree_skb(skb);
	}
	seq_puts(s, "}\n");

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(txdesc);

/*---------Tx/Rx status message------------*/
static int status_msg_show(struct seq_file *s, void *data)
{
	struct wil6210_priv *wil = s->private;
	int sring_idx = dbg_sring_index;
	struct wil_status_ring *sring;
	bool tx;
	u32 status_msg_idx = dbg_status_msg_index;
	u32 *u;

	if (sring_idx >= WIL6210_MAX_STATUS_RINGS) {
		seq_printf(s, "invalid status ring index %d\n", sring_idx);
		return 0;
	}

	sring = &wil->srings[sring_idx];
	tx = !sring->is_rx;

	if (!sring->va) {
		seq_printf(s, "No %cX status ring\n", tx ? 'T' : 'R');
		return 0;
	}

	if (status_msg_idx >= sring->size) {
		seq_printf(s, "%cxDesc index (%d) >= size (%d)\n",
			   tx ? 'T' : 'R', status_msg_idx, sring->size);
		return 0;
	}

	u = sring->va + (sring->elem_size * status_msg_idx);

	seq_printf(s, "%cx[%d][%3d] = {\n",
		   tx ? 'T' : 'R', sring_idx, status_msg_idx);

	seq_printf(s, "  0x%08x 0x%08x 0x%08x 0x%08x\n",
		   u[0], u[1], u[2], u[3]);
	if (!tx && !wil->use_compressed_rx_status)
		seq_printf(s, "  0x%08x 0x%08x 0x%08x 0x%08x\n",
			   u[4], u[5], u[6], u[7]);

	seq_puts(s, "}\n");

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(status_msg);

static int wil_print_rx_buff(struct seq_file *s, struct list_head *lh)
{
	struct wil_rx_buff *it;
	int i = 0;

	list_for_each_entry(it, lh, list) {
		if ((i % 16) == 0 && i != 0)
			seq_puts(s, "\n    ");
		seq_printf(s, "[%4d] ", it->id);
		i++;
	}
	seq_printf(s, "\nNumber of buffers: %u\n", i);

	return i;
}

static int rx_buff_mgmt_show(struct seq_file *s, void *data)
{
	struct wil6210_priv *wil = s->private;
	struct wil_rx_buff_mgmt *rbm = &wil->rx_buff_mgmt;
	int num_active;
	int num_free;

	if (!rbm->buff_arr)
		return -EINVAL;

	seq_printf(s, "  size = %zu\n", rbm->size);
	seq_printf(s, "  free_list_empty_cnt = %lu\n",
		   rbm->free_list_empty_cnt);

	/* Print active list */
	seq_puts(s, "  Active list:\n");
	num_active = wil_print_rx_buff(s, &rbm->active);
	seq_puts(s, "\n  Free list:\n");
	num_free = wil_print_rx_buff(s, &rbm->free);

	seq_printf(s, "  Total number of buffers: %u\n",
		   num_active + num_free);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(rx_buff_mgmt);

/*---------beamforming------------*/
static char *wil_bfstatus_str(u32 status)
{
	switch (status) {
	case 0:
		return "Failed";
	case 1:
		return "OK";
	case 2:
		return "Retrying";
	default:
		return "??";
	}
}

static bool is_all_zeros(void * const x_, size_t sz)
{
	/* if reply is all-0, ignore this CID */
	u32 *x = x_;
	int n;

	for (n = 0; n < sz / sizeof(*x); n++)
		if (x[n])
			return false;

	return true;
}

static int bf_show(struct seq_file *s, void *data)
{
	int rc;
	int i;
	struct wil6210_priv *wil = s->private;
	struct wil6210_vif *vif = ndev_to_vif(wil->main_ndev);
	struct wmi_notify_req_cmd cmd = {
		.interval_usec = 0,
	};
	struct {
		struct wmi_cmd_hdr wmi;
		struct wmi_notify_req_done_event evt;
	} __packed reply;

	memset(&reply, 0, sizeof(reply));

	for (i = 0; i < wil->max_assoc_sta; i++) {
		u32 status;
		u8 bf_mcs;

		cmd.cid = i;
		rc = wmi_call(wil, WMI_NOTIFY_REQ_CMDID, vif->mid,
			      &cmd, sizeof(cmd),
			      WMI_NOTIFY_REQ_DONE_EVENTID, &reply,
			      sizeof(reply), WIL_WMI_CALL_GENERAL_TO_MS);
		/* if reply is all-0, ignore this CID */
		if (rc || is_all_zeros(&reply.evt, sizeof(reply.evt)))
			continue;

		status = le32_to_cpu(reply.evt.status);
		bf_mcs = le16_to_cpu(reply.evt.bf_mcs);
		seq_printf(s, "CID %d {\n"
			   "  TSF = 0x%016llx\n"
			   "  TxMCS = %s TxTpt = %4d\n"
			   "  SQI = %4d\n"
			   "  RSSI = %4d\n"
			   "  Status = 0x%08x %s\n"
			   "  Sectors(rx:tx) my %2d:%2d peer %2d:%2d\n"
			   "  Goodput(rx:tx) %4d:%4d\n"
			   "}\n",
			   i,
			   le64_to_cpu(reply.evt.tsf),
			   WIL_EXTENDED_MCS_CHECK(bf_mcs),
			   le32_to_cpu(reply.evt.tx_tpt),
			   reply.evt.sqi,
			   reply.evt.rssi,
			   status, wil_bfstatus_str(status),
			   le16_to_cpu(reply.evt.my_rx_sector),
			   le16_to_cpu(reply.evt.my_tx_sector),
			   le16_to_cpu(reply.evt.other_rx_sector),
			   le16_to_cpu(reply.evt.other_tx_sector),
			   le32_to_cpu(reply.evt.rx_goodput),
			   le32_to_cpu(reply.evt.tx_goodput));
	}
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(bf);

/*---------temp------------*/
static void print_temp(struct seq_file *s, const char *prefix, s32 t)
{
	switch (t) {
	case 0:
	case WMI_INVALID_TEMPERATURE:
		seq_printf(s, "%s N/A\n", prefix);
	break;
	default:
		seq_printf(s, "%s %s%d.%03d\n", prefix, (t < 0 ? "-" : ""),
			   abs(t / 1000), abs(t % 1000));
		break;
	}
}

static int temp_show(struct seq_file *s, void *data)
{
	struct wil6210_priv *wil = s->private;
	int rc, i;

	if (test_bit(WMI_FW_CAPABILITY_TEMPERATURE_ALL_RF,
		     wil->fw_capabilities)) {
		struct wmi_temp_sense_all_done_event sense_all_evt;

		wil_dbg_misc(wil,
			     "WMI_FW_CAPABILITY_TEMPERATURE_ALL_RF is supported");
		rc = wmi_get_all_temperatures(wil, &sense_all_evt);
		if (rc) {
			seq_puts(s, "Failed\n");
			return 0;
		}
		print_temp(s, "T_mac   =",
			   le32_to_cpu(sense_all_evt.baseband_t1000));
		seq_printf(s, "Connected RFs [0x%08x]\n",
			   sense_all_evt.rf_bitmap);
		for (i = 0; i < WMI_MAX_XIF_PORTS_NUM; i++) {
			seq_printf(s, "RF[%d]   = ", i);
			print_temp(s, "",
				   le32_to_cpu(sense_all_evt.rf_t1000[i]));
		}
	} else {
		s32 t_m, t_r;

		wil_dbg_misc(wil,
			     "WMI_FW_CAPABILITY_TEMPERATURE_ALL_RF is not supported");
		rc = wmi_get_temperature(wil, &t_m, &t_r);
		if (rc) {
			seq_puts(s, "Failed\n");
			return 0;
		}
		print_temp(s, "T_mac   =", t_m);
		print_temp(s, "T_radio =", t_r);
	}
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(temp);

/*---------link------------*/
static int link_show(struct seq_file *s, void *data)
{
	struct wil6210_priv *wil = s->private;
	struct station_info *sinfo;
	int i, rc = 0;

	sinfo = kzalloc(sizeof(*sinfo), GFP_KERNEL);
	if (!sinfo)
		return -ENOMEM;

	for (i = 0; i < wil->max_assoc_sta; i++) {
		struct wil_sta_info *p = &wil->sta[i];
		char *status = "unknown";
		struct wil6210_vif *vif;
		u8 mid;

		switch (p->status) {
		case wil_sta_unused:
			status = "unused   ";
			break;
		case wil_sta_conn_pending:
			status = "pending  ";
			break;
		case wil_sta_connected:
			status = "connected";
			break;
		}
		mid = (p->status != wil_sta_unused) ? p->mid : U8_MAX;
		seq_printf(s, "[%d][MID %d] %pM %s\n",
			   i, mid, p->addr, status);

		if (p->status != wil_sta_connected)
			continue;

		vif = (mid < GET_MAX_VIFS(wil)) ? wil->vifs[mid] : NULL;
		if (vif) {
			rc = wil_cid_fill_sinfo(vif, i, sinfo);
			if (rc)
				goto out;

			seq_printf(s, "  Tx_mcs = %s\n",
				   WIL_EXTENDED_MCS_CHECK(sinfo->txrate.mcs));
			seq_printf(s, "  Rx_mcs = %s\n",
				   WIL_EXTENDED_MCS_CHECK(sinfo->rxrate.mcs));
			seq_printf(s, "  SQ     = %d\n", sinfo->signal);
		} else {
			seq_puts(s, "  INVALID MID\n");
		}
	}

out:
	kfree(sinfo);
	return rc;
}
DEFINE_SHOW_ATTRIBUTE(link);

/*---------info------------*/
static int info_show(struct seq_file *s, void *data)
{
	struct wil6210_priv *wil = s->private;
	struct net_device *ndev = wil->main_ndev;
	int is_ac = power_supply_is_system_supplied();
	int rx = atomic_xchg(&wil->isr_count_rx, 0);
	int tx = atomic_xchg(&wil->isr_count_tx, 0);
	static ulong rxf_old, txf_old;
	ulong rxf = ndev->stats.rx_packets;
	ulong txf = ndev->stats.tx_packets;
	unsigned int i;

	/* >0 : AC; 0 : battery; <0 : error */
	seq_printf(s, "AC powered : %d\n", is_ac);
	seq_printf(s, "Rx irqs:packets : %8d : %8ld\n", rx, rxf - rxf_old);
	seq_printf(s, "Tx irqs:packets : %8d : %8ld\n", tx, txf - txf_old);
	rxf_old = rxf;
	txf_old = txf;

#define CHECK_QSTATE(x) (state & BIT(__QUEUE_STATE_ ## x)) ? \
	" " __stringify(x) : ""

	for (i = 0; i < ndev->num_tx_queues; i++) {
		struct netdev_queue *txq = netdev_get_tx_queue(ndev, i);
		unsigned long state = txq->state;

		seq_printf(s, "Tx queue[%i] state : 0x%lx%s%s%s\n", i, state,
			   CHECK_QSTATE(DRV_XOFF),
			   CHECK_QSTATE(STACK_XOFF),
			   CHECK_QSTATE(FROZEN)
			  );
	}
#undef CHECK_QSTATE
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(info);

/*---------recovery------------*/
/* mode = [manual|auto]
 * state = [idle|pending|running]
 */
static ssize_t wil_read_file_recovery(struct file *file, char __user *user_buf,
				      size_t count, loff_t *ppos)
{
	struct wil6210_priv *wil = file->private_data;
	char buf[80];
	int n;
	static const char * const sstate[] = {"idle", "pending", "running"};

	n = snprintf(buf, sizeof(buf), "mode = %s\nstate = %s\n",
		     no_fw_recovery ? "manual" : "auto",
		     sstate[wil->recovery_state]);

	n = min_t(int, n, sizeof(buf));

	return simple_read_from_buffer(user_buf, count, ppos,
				       buf, n);
}

static ssize_t wil_write_file_recovery(struct file *file,
				       const char __user *buf_,
				       size_t count, loff_t *ppos)
{
	struct wil6210_priv *wil = file->private_data;
	static const char run_command[] = "run";
	char buf[sizeof(run_command) + 1]; /* to detect "runx" */
	ssize_t rc;

	if (wil->recovery_state != fw_recovery_pending) {
		wil_err(wil, "No recovery pending\n");
		return -EINVAL;
	}

	if (*ppos != 0) {
		wil_err(wil, "Offset [%d]\n", (int)*ppos);
		return -EINVAL;
	}

	if (count > sizeof(buf)) {
		wil_err(wil, "Input too long, len = %d\n", (int)count);
		return -EINVAL;
	}

	rc = simple_write_to_buffer(buf, sizeof(buf) - 1, ppos, buf_, count);
	if (rc < 0)
		return rc;

	buf[rc] = '\0';
	if (0 == strcmp(buf, run_command))
		wil_set_recovery_state(wil, fw_recovery_running);
	else
		wil_err(wil, "Bad recovery command \"%s\"\n", buf);

	return rc;
}

static const struct file_operations fops_recovery = {
	.read = wil_read_file_recovery,
	.write = wil_write_file_recovery,
	.open  = simple_open,
};

/*---------Station matrix------------*/
static void wil_print_rxtid(struct seq_file *s, struct wil_tid_ampdu_rx *r)
{
	int i;
	u16 index = ((r->head_seq_num - r->ssn) & 0xfff) % r->buf_size;
	unsigned long long drop_dup = r->drop_dup, drop_old = r->drop_old;
	unsigned long long drop_dup_mcast = r->drop_dup_mcast;

	seq_printf(s, "([%2d]) 0x%03x [", r->buf_size, r->head_seq_num);
	for (i = 0; i < r->buf_size; i++) {
		if (i == index)
			seq_printf(s, "%c", r->reorder_buf[i] ? 'O' : '|');
		else
			seq_printf(s, "%c", r->reorder_buf[i] ? '*' : '_');
	}
	seq_printf(s,
		   "] total %llu drop %llu (dup %llu + old %llu + dup mcast %llu) last 0x%03x\n",
		   r->total, drop_dup + drop_old + drop_dup_mcast, drop_dup,
		   drop_old, drop_dup_mcast, r->ssn_last_drop);
}

static void wil_print_rxtid_crypto(struct seq_file *s, int tid,
				   struct wil_tid_crypto_rx *c)
{
	int i;

	for (i = 0; i < 4; i++) {
		struct wil_tid_crypto_rx_single *cc = &c->key_id[i];

		if (cc->key_set)
			goto has_keys;
	}
	return;

has_keys:
	if (tid < WIL_STA_TID_NUM)
		seq_printf(s, "  [%2d] PN", tid);
	else
		seq_puts(s, "  [GR] PN");

	for (i = 0; i < 4; i++) {
		struct wil_tid_crypto_rx_single *cc = &c->key_id[i];

		seq_printf(s, " [%i%s]%6phN", i, cc->key_set ? "+" : "-",
			   cc->pn);
	}
	seq_puts(s, "\n");
}

static int sta_show(struct seq_file *s, void *data)
__acquires(&p->tid_rx_lock) __releases(&p->tid_rx_lock)
{
	struct wil6210_priv *wil = s->private;
	int i, tid, mcs;

	for (i = 0; i < wil->max_assoc_sta; i++) {
		struct wil_sta_info *p = &wil->sta[i];
		char *status = "unknown";
		u8 aid = 0;
		u8 mid;
		bool sta_connected = false;

		switch (p->status) {
		case wil_sta_unused:
			status = "unused   ";
			break;
		case wil_sta_conn_pending:
			status = "pending  ";
			break;
		case wil_sta_connected:
			status = "connected";
			aid = p->aid;
			break;
		}
		mid = (p->status != wil_sta_unused) ? p->mid : U8_MAX;
		if (mid < GET_MAX_VIFS(wil)) {
			struct wil6210_vif *vif = wil->vifs[mid];

			if (vif->wdev.iftype == NL80211_IFTYPE_STATION &&
			    p->status == wil_sta_connected)
				sta_connected = true;
		}
		/* print roam counter only for connected stations */
		if (sta_connected)
			seq_printf(s, "[%d] %pM connected (roam counter %d) MID %d AID %d\n",
				   i, p->addr, p->stats.ft_roams, mid, aid);
		else
			seq_printf(s, "[%d] %pM %s MID %d AID %d\n", i,
				   p->addr, status, mid, aid);

		if (p->status == wil_sta_connected) {
			spin_lock_bh(&p->tid_rx_lock);
			for (tid = 0; tid < WIL_STA_TID_NUM; tid++) {
				struct wil_tid_ampdu_rx *r = p->tid_rx[tid];
				struct wil_tid_crypto_rx *c =
						&p->tid_crypto_rx[tid];

				if (r) {
					seq_printf(s, "  [%2d] ", tid);
					wil_print_rxtid(s, r);
				}

				wil_print_rxtid_crypto(s, tid, c);
			}
			wil_print_rxtid_crypto(s, WIL_STA_TID_NUM,
					       &p->group_crypto_rx);
			spin_unlock_bh(&p->tid_rx_lock);
			seq_printf(s,
				   "Rx invalid frame: non-data %lu, short %lu, large %lu, replay %lu\n",
				   p->stats.rx_non_data_frame,
				   p->stats.rx_short_frame,
				   p->stats.rx_large_frame,
				   p->stats.rx_replay);
			seq_printf(s,
				   "mic error %lu, key error %lu, amsdu error %lu, csum error %lu\n",
				   p->stats.rx_mic_error,
				   p->stats.rx_key_error,
				   p->stats.rx_amsdu_error,
				   p->stats.rx_csum_err);

			seq_puts(s, "Rx/MCS:");
			for (mcs = 0; mcs < ARRAY_SIZE(p->stats.rx_per_mcs);
			     mcs++)
				seq_printf(s, " %lld",
					   p->stats.rx_per_mcs[mcs]);
			seq_puts(s, "\n");
		}
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(sta);

static int mids_show(struct seq_file *s, void *data)
{
	struct wil6210_priv *wil = s->private;
	struct wil6210_vif *vif;
	struct net_device *ndev;
	int i;

	mutex_lock(&wil->vif_mutex);
	for (i = 0; i < GET_MAX_VIFS(wil); i++) {
		vif = wil->vifs[i];

		if (vif) {
			ndev = vif_to_ndev(vif);
			seq_printf(s, "[%d] %pM %s\n", i, ndev->dev_addr,
				   ndev->name);
		} else {
			seq_printf(s, "[%d] unused\n", i);
		}
	}
	mutex_unlock(&wil->vif_mutex);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(mids);

static int wil_tx_latency_debugfs_show(struct seq_file *s, void *data)
__acquires(&p->tid_rx_lock) __releases(&p->tid_rx_lock)
{
	struct wil6210_priv *wil = s->private;
	int i, bin;

	for (i = 0; i < wil->max_assoc_sta; i++) {
		struct wil_sta_info *p = &wil->sta[i];
		char *status = "unknown";
		u8 aid = 0;
		u8 mid;

		if (!p->tx_latency_bins)
			continue;

		switch (p->status) {
		case wil_sta_unused:
			status = "unused   ";
			break;
		case wil_sta_conn_pending:
			status = "pending  ";
			break;
		case wil_sta_connected:
			status = "connected";
			aid = p->aid;
			break;
		}
		mid = (p->status != wil_sta_unused) ? p->mid : U8_MAX;
		seq_printf(s, "[%d] %pM %s MID %d AID %d\n", i, p->addr, status,
			   mid, aid);

		if (p->status == wil_sta_connected) {
			u64 num_packets = 0;
			u64 tx_latency_avg = p->stats.tx_latency_total_us;

			seq_puts(s, "Tx/Latency bin:");
			for (bin = 0; bin < WIL_NUM_LATENCY_BINS; bin++) {
				seq_printf(s, " %lld",
					   p->tx_latency_bins[bin]);
				num_packets += p->tx_latency_bins[bin];
			}
			seq_puts(s, "\n");
			if (!num_packets)
				continue;
			do_div(tx_latency_avg, num_packets);
			seq_printf(s, "Tx/Latency min/avg/max (us): %d/%lld/%d",
				   p->stats.tx_latency_min_us,
				   tx_latency_avg,
				   p->stats.tx_latency_max_us);

			seq_puts(s, "\n");
		}
	}

	return 0;
}

static int wil_tx_latency_seq_open(struct inode *inode, struct file *file)
{
	return single_open(file, wil_tx_latency_debugfs_show,
			   inode->i_private);
}

static ssize_t wil_tx_latency_write(struct file *file, const char __user *buf,
				    size_t len, loff_t *ppos)
{
	struct seq_file *s = file->private_data;
	struct wil6210_priv *wil = s->private;
	int val, rc, i;
	bool enable;

	rc = kstrtoint_from_user(buf, len, 0, &val);
	if (rc) {
		wil_err(wil, "Invalid argument\n");
		return rc;
	}
	if (val == 1)
		/* default resolution */
		val = 500;
	if (val && (val < 50 || val > 1000)) {
		wil_err(wil, "Invalid resolution %d\n", val);
		return -EINVAL;
	}

	enable = !!val;
	if (wil->tx_latency == enable)
		return len;

	wil_info(wil, "%s TX latency measurements (resolution %dusec)\n",
		 enable ? "Enabling" : "Disabling", val);

	if (enable) {
		size_t sz = sizeof(u64) * WIL_NUM_LATENCY_BINS;

		wil->tx_latency_res = val;
		for (i = 0; i < wil->max_assoc_sta; i++) {
			struct wil_sta_info *sta = &wil->sta[i];

			kfree(sta->tx_latency_bins);
			sta->tx_latency_bins = kzalloc(sz, GFP_KERNEL);
			if (!sta->tx_latency_bins)
				return -ENOMEM;
			sta->stats.tx_latency_min_us = U32_MAX;
			sta->stats.tx_latency_max_us = 0;
			sta->stats.tx_latency_total_us = 0;
		}
	}
	wil->tx_latency = enable;

	return len;
}

static const struct file_operations fops_tx_latency = {
	.open		= wil_tx_latency_seq_open,
	.release	= single_release,
	.read		= seq_read,
	.write		= wil_tx_latency_write,
	.llseek		= seq_lseek,
};

static void wil_link_stats_print_basic(struct wil6210_vif *vif,
				       struct seq_file *s,
				       struct wmi_link_stats_basic *basic)
{
	char per[5] = "?";

	if (basic->per_average != 0xff)
		snprintf(per, sizeof(per), "%d%%", basic->per_average);

	seq_printf(s, "CID %d {\n"
		   "\tTxMCS %s TxTpt %d\n"
		   "\tGoodput(rx:tx) %d:%d\n"
		   "\tRxBcastFrames %d\n"
		   "\tRSSI %d SQI %d SNR %d PER %s\n"
		   "\tRx RFC %d Ant num %d\n"
		   "\tSectors(rx:tx) my %d:%d peer %d:%d\n"
		   "}\n",
		   basic->cid,
		   WIL_EXTENDED_MCS_CHECK(basic->bf_mcs),
		   le32_to_cpu(basic->tx_tpt),
		   le32_to_cpu(basic->rx_goodput),
		   le32_to_cpu(basic->tx_goodput),
		   le32_to_cpu(basic->rx_bcast_frames),
		   basic->rssi, basic->sqi, basic->snr, per,
		   basic->selected_rfc, basic->rx_effective_ant_num,
		   basic->my_rx_sector, basic->my_tx_sector,
		   basic->other_rx_sector, basic->other_tx_sector);
}

static void wil_link_stats_print_global(struct wil6210_priv *wil,
					struct seq_file *s,
					struct wmi_link_stats_global *global)
{
	seq_printf(s, "Frames(rx:tx) %d:%d\n"
		   "BA Frames(rx:tx) %d:%d\n"
		   "Beacons %d\n"
		   "Rx Errors (MIC:CRC) %d:%d\n"
		   "Tx Errors (no ack) %d\n",
		   le32_to_cpu(global->rx_frames),
		   le32_to_cpu(global->tx_frames),
		   le32_to_cpu(global->rx_ba_frames),
		   le32_to_cpu(global->tx_ba_frames),
		   le32_to_cpu(global->tx_beacons),
		   le32_to_cpu(global->rx_mic_errors),
		   le32_to_cpu(global->rx_crc_errors),
		   le32_to_cpu(global->tx_fail_no_ack));
}

static void wil_link_stats_debugfs_show_vif(struct wil6210_vif *vif,
					    struct seq_file *s)
{
	struct wil6210_priv *wil = vif_to_wil(vif);
	struct wmi_link_stats_basic *stats;
	int i;

	if (!vif->fw_stats_ready) {
		seq_puts(s, "no statistics\n");
		return;
	}

	seq_printf(s, "TSF %lld\n", vif->fw_stats_tsf);
	for (i = 0; i < wil->max_assoc_sta; i++) {
		if (wil->sta[i].status == wil_sta_unused)
			continue;
		if (wil->sta[i].mid != vif->mid)
			continue;

		stats = &wil->sta[i].fw_stats_basic;
		wil_link_stats_print_basic(vif, s, stats);
	}
}

static int wil_link_stats_debugfs_show(struct seq_file *s, void *data)
{
	struct wil6210_priv *wil = s->private;
	struct wil6210_vif *vif;
	int i, rc;

	rc = mutex_lock_interruptible(&wil->vif_mutex);
	if (rc)
		return rc;

	/* iterate over all MIDs and show per-cid statistics. Then show the
	 * global statistics
	 */
	for (i = 0; i < GET_MAX_VIFS(wil); i++) {
		vif = wil->vifs[i];

		seq_printf(s, "MID %d ", i);
		if (!vif) {
			seq_puts(s, "unused\n");
			continue;
		}

		wil_link_stats_debugfs_show_vif(vif, s);
	}

	mutex_unlock(&wil->vif_mutex);

	return 0;
}

static int wil_link_stats_seq_open(struct inode *inode, struct file *file)
{
	return single_open(file, wil_link_stats_debugfs_show, inode->i_private);
}

static ssize_t wil_link_stats_write(struct file *file, const char __user *buf,
				    size_t len, loff_t *ppos)
{
	struct seq_file *s = file->private_data;
	struct wil6210_priv *wil = s->private;
	int cid, interval, rc, i;
	struct wil6210_vif *vif;
	char *kbuf = kmalloc(len + 1, GFP_KERNEL);

	if (!kbuf)
		return -ENOMEM;

	rc = simple_write_to_buffer(kbuf, len, ppos, buf, len);
	if (rc != len) {
		kfree(kbuf);
		return rc >= 0 ? -EIO : rc;
	}

	kbuf[len] = '\0';
	/* specify cid (use -1 for all cids) and snapshot interval in ms */
	rc = sscanf(kbuf, "%d %d", &cid, &interval);
	kfree(kbuf);
	if (rc < 0)
		return rc;
	if (rc < 2 || interval < 0)
		return -EINVAL;

	wil_info(wil, "request link statistics, cid %d interval %d\n",
		 cid, interval);

	rc = mutex_lock_interruptible(&wil->vif_mutex);
	if (rc)
		return rc;

	for (i = 0; i < GET_MAX_VIFS(wil); i++) {
		vif = wil->vifs[i];
		if (!vif)
			continue;

		rc = wmi_link_stats_cfg(vif, WMI_LINK_STATS_TYPE_BASIC,
					(cid == -1 ? 0xff : cid), interval);
		if (rc)
			wil_err(wil, "link statistics failed for mid %d\n", i);
	}
	mutex_unlock(&wil->vif_mutex);

	return len;
}

static const struct file_operations fops_link_stats = {
	.open		= wil_link_stats_seq_open,
	.release	= single_release,
	.read		= seq_read,
	.write		= wil_link_stats_write,
	.llseek		= seq_lseek,
};

static int
wil_link_stats_global_debugfs_show(struct seq_file *s, void *data)
{
	struct wil6210_priv *wil = s->private;

	if (!wil->fw_stats_global.ready)
		return 0;

	seq_printf(s, "TSF %lld\n", wil->fw_stats_global.tsf);
	wil_link_stats_print_global(wil, s, &wil->fw_stats_global.stats);

	return 0;
}

static int
wil_link_stats_global_seq_open(struct inode *inode, struct file *file)
{
	return single_open(file, wil_link_stats_global_debugfs_show,
			   inode->i_private);
}

static ssize_t
wil_link_stats_global_write(struct file *file, const char __user *buf,
			    size_t len, loff_t *ppos)
{
	struct seq_file *s = file->private_data;
	struct wil6210_priv *wil = s->private;
	int interval, rc;
	struct wil6210_vif *vif = ndev_to_vif(wil->main_ndev);

	/* specify snapshot interval in ms */
	rc = kstrtoint_from_user(buf, len, 0, &interval);
	if (rc || interval < 0) {
		wil_err(wil, "Invalid argument\n");
		return -EINVAL;
	}

	wil_info(wil, "request global link stats, interval %d\n", interval);

	rc = wmi_link_stats_cfg(vif, WMI_LINK_STATS_TYPE_GLOBAL, 0, interval);
	if (rc)
		wil_err(wil, "global link stats failed %d\n", rc);

	return rc ? rc : len;
}

static const struct file_operations fops_link_stats_global = {
	.open		= wil_link_stats_global_seq_open,
	.release	= single_release,
	.read		= seq_read,
	.write		= wil_link_stats_global_write,
	.llseek		= seq_lseek,
};

static ssize_t wil_read_file_led_cfg(struct file *file, char __user *user_buf,
				     size_t count, loff_t *ppos)
{
	char buf[80];
	int n;

	n = snprintf(buf, sizeof(buf),
		     "led_id is set to %d, echo 1 to enable, 0 to disable\n",
		     led_id);

	n = min_t(int, n, sizeof(buf));

	return simple_read_from_buffer(user_buf, count, ppos,
				       buf, n);
}

static ssize_t wil_write_file_led_cfg(struct file *file,
				      const char __user *buf_,
				      size_t count, loff_t *ppos)
{
	struct wil6210_priv *wil = file->private_data;
	int val;
	int rc;

	rc = kstrtoint_from_user(buf_, count, 0, &val);
	if (rc) {
		wil_err(wil, "Invalid argument\n");
		return rc;
	}

	wil_info(wil, "%s led %d\n", val ? "Enabling" : "Disabling", led_id);
	rc = wmi_led_cfg(wil, val);
	if (rc) {
		wil_info(wil, "%s led %d failed\n",
			 val ? "Enabling" : "Disabling", led_id);
		return rc;
	}

	return count;
}

static const struct file_operations fops_led_cfg = {
	.read = wil_read_file_led_cfg,
	.write = wil_write_file_led_cfg,
	.open  = simple_open,
};

/* led_blink_time, write:
 * "<blink_on_slow> <blink_off_slow> <blink_on_med> <blink_off_med> <blink_on_fast> <blink_off_fast>
 */
static ssize_t wil_write_led_blink_time(struct file *file,
					const char __user *buf,
					size_t len, loff_t *ppos)
{
	int rc;
	char *kbuf = kmalloc(len + 1, GFP_KERNEL);

	if (!kbuf)
		return -ENOMEM;

	rc = simple_write_to_buffer(kbuf, len, ppos, buf, len);
	if (rc != len) {
		kfree(kbuf);
		return rc >= 0 ? -EIO : rc;
	}

	kbuf[len] = '\0';
	rc = sscanf(kbuf, "%d %d %d %d %d %d",
		    &led_blink_time[WIL_LED_TIME_SLOW].on_ms,
		    &led_blink_time[WIL_LED_TIME_SLOW].off_ms,
		    &led_blink_time[WIL_LED_TIME_MED].on_ms,
		    &led_blink_time[WIL_LED_TIME_MED].off_ms,
		    &led_blink_time[WIL_LED_TIME_FAST].on_ms,
		    &led_blink_time[WIL_LED_TIME_FAST].off_ms);
	kfree(kbuf);

	if (rc < 0)
		return rc;
	if (rc < 6)
		return -EINVAL;

	return len;
}

static ssize_t wil_read_led_blink_time(struct file *file, char __user *user_buf,
				       size_t count, loff_t *ppos)
{
	static char text[400];

	snprintf(text, sizeof(text),
		 "To set led blink on/off time variables write:\n"
		 "<blink_on_slow> <blink_off_slow> <blink_on_med> "
		 "<blink_off_med> <blink_on_fast> <blink_off_fast>\n"
		 "The current values are:\n"
		 "%d %d %d %d %d %d\n",
		 led_blink_time[WIL_LED_TIME_SLOW].on_ms,
		 led_blink_time[WIL_LED_TIME_SLOW].off_ms,
		 led_blink_time[WIL_LED_TIME_MED].on_ms,
		 led_blink_time[WIL_LED_TIME_MED].off_ms,
		 led_blink_time[WIL_LED_TIME_FAST].on_ms,
		 led_blink_time[WIL_LED_TIME_FAST].off_ms);

	return simple_read_from_buffer(user_buf, count, ppos, text,
				       sizeof(text));
}

static const struct file_operations fops_led_blink_time = {
	.read = wil_read_led_blink_time,
	.write = wil_write_led_blink_time,
	.open  = simple_open,
};

/*---------FW capabilities------------*/
static int fw_capabilities_show(struct seq_file *s, void *data)
{
	struct wil6210_priv *wil = s->private;

	seq_printf(s, "fw_capabilities : %*pb\n", WMI_FW_CAPABILITY_MAX,
		   wil->fw_capabilities);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(fw_capabilities);

/*---------FW version------------*/
static int fw_version_show(struct seq_file *s, void *data)
{
	struct wil6210_priv *wil = s->private;

	if (wil->fw_version[0])
		seq_printf(s, "%s\n", wil->fw_version);
	else
		seq_puts(s, "N/A\n");

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(fw_version);

/*---------suspend_stats---------*/
static ssize_t wil_write_suspend_stats(struct file *file,
				       const char __user *buf,
				       size_t len, loff_t *ppos)
{
	struct wil6210_priv *wil = file->private_data;

	memset(&wil->suspend_stats, 0, sizeof(wil->suspend_stats));

	return len;
}

static ssize_t wil_read_suspend_stats(struct file *file,
				      char __user *user_buf,
				      size_t count, loff_t *ppos)
{
	struct wil6210_priv *wil = file->private_data;
	char *text;
	int n, ret, text_size = 500;

	text = kmalloc(text_size, GFP_KERNEL);
	if (!text)
		return -ENOMEM;

	n = snprintf(text, text_size,
		     "Radio on suspend statistics:\n"
		     "successful suspends:%ld failed suspends:%ld\n"
		     "successful resumes:%ld failed resumes:%ld\n"
		     "rejected by device:%ld\n"
		     "Radio off suspend statistics:\n"
		     "successful suspends:%ld failed suspends:%ld\n"
		     "successful resumes:%ld failed resumes:%ld\n"
		     "General statistics:\n"
		     "rejected by host:%ld\n",
		     wil->suspend_stats.r_on.successful_suspends,
		     wil->suspend_stats.r_on.failed_suspends,
		     wil->suspend_stats.r_on.successful_resumes,
		     wil->suspend_stats.r_on.failed_resumes,
		     wil->suspend_stats.rejected_by_device,
		     wil->suspend_stats.r_off.successful_suspends,
		     wil->suspend_stats.r_off.failed_suspends,
		     wil->suspend_stats.r_off.successful_resumes,
		     wil->suspend_stats.r_off.failed_resumes,
		     wil->suspend_stats.rejected_by_host);

	n = min_t(int, n, text_size);

	ret = simple_read_from_buffer(user_buf, count, ppos, text, n);

	kfree(text);

	return ret;
}

static const struct file_operations fops_suspend_stats = {
	.read = wil_read_suspend_stats,
	.write = wil_write_suspend_stats,
	.open  = simple_open,
};

/*---------compressed_rx_status---------*/
static ssize_t wil_compressed_rx_status_write(struct file *file,
					      const char __user *buf,
					      size_t len, loff_t *ppos)
{
	struct seq_file *s = file->private_data;
	struct wil6210_priv *wil = s->private;
	int compressed_rx_status;
	int rc;

	rc = kstrtoint_from_user(buf, len, 0, &compressed_rx_status);
	if (rc) {
		wil_err(wil, "Invalid argument\n");
		return rc;
	}

	if (wil_has_active_ifaces(wil, true, false)) {
		wil_err(wil, "cannot change edma config after iface is up\n");
		return -EPERM;
	}

	wil_info(wil, "%sable compressed_rx_status\n",
		 compressed_rx_status ? "En" : "Dis");

	wil->use_compressed_rx_status = compressed_rx_status;

	return len;
}

static int
wil_compressed_rx_status_show(struct seq_file *s, void *data)
{
	struct wil6210_priv *wil = s->private;

	seq_printf(s, "%d\n", wil->use_compressed_rx_status);

	return 0;
}

static int
wil_compressed_rx_status_seq_open(struct inode *inode, struct file *file)
{
	return single_open(file, wil_compressed_rx_status_show,
			   inode->i_private);
}

static const struct file_operations fops_compressed_rx_status = {
	.open  = wil_compressed_rx_status_seq_open,
	.release = single_release,
	.read = seq_read,
	.write = wil_compressed_rx_status_write,
	.llseek	= seq_lseek,
};

/*----------------*/
static void wil6210_debugfs_init_blobs(struct wil6210_priv *wil,
				       struct dentry *dbg)
{
	int i;
	char name[32];

	for (i = 0; i < ARRAY_SIZE(fw_mapping); i++) {
		struct wil_blob_wrapper *wil_blob = &wil->blobs[i];
		struct debugfs_blob_wrapper *blob = &wil_blob->blob;
		const struct fw_map *map = &fw_mapping[i];

		if (!map->name)
			continue;

		wil_blob->wil = wil;
		blob->data = (void * __force)wil->csr + HOSTADDR(map->host);
		blob->size = map->to - map->from;
		snprintf(name, sizeof(name), "blob_%s", map->name);
		wil_debugfs_create_ioblob(name, 0444, dbg, wil_blob);
	}
}

/* misc files */
static const struct {
	const char *name;
	umode_t mode;
	const struct file_operations *fops;
} dbg_files[] = {
	{"mbox",	0444,		&mbox_fops},
	{"rings",	0444,		&ring_fops},
	{"stations", 0444,		&sta_fops},
	{"mids",	0444,		&mids_fops},
	{"desc",	0444,		&txdesc_fops},
	{"bf",		0444,		&bf_fops},
	{"mem_val",	0644,		&memread_fops},
	{"rxon",	0244,		&fops_rxon},
	{"tx_mgmt",	0244,		&fops_txmgmt},
	{"wmi_send", 0244,		&fops_wmi},
	{"back",	0644,		&fops_back},
	{"pmccfg",	0644,		&fops_pmccfg},
	{"pmcdata",	0444,		&fops_pmcdata},
	{"pmcring",	0444,		&fops_pmcring},
	{"temp",	0444,		&temp_fops},
	{"link",	0444,		&link_fops},
	{"info",	0444,		&info_fops},
	{"recovery", 0644,		&fops_recovery},
	{"led_cfg",	0644,		&fops_led_cfg},
	{"led_blink_time",	0644,	&fops_led_blink_time},
	{"fw_capabilities",	0444,	&fw_capabilities_fops},
	{"fw_version",	0444,		&fw_version_fops},
	{"suspend_stats",	0644,	&fops_suspend_stats},
	{"compressed_rx_status", 0644,	&fops_compressed_rx_status},
	{"srings",	0444,		&srings_fops},
	{"status_msg",	0444,		&status_msg_fops},
	{"rx_buff_mgmt",	0444,	&rx_buff_mgmt_fops},
	{"tx_latency",	0644,		&fops_tx_latency},
	{"link_stats",	0644,		&fops_link_stats},
	{"link_stats_global",	0644,	&fops_link_stats_global},
	{"rbufcap",	0244,		&fops_rbufcap},
};

static void wil6210_debugfs_init_files(struct wil6210_priv *wil,
				       struct dentry *dbg)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(dbg_files); i++)
		debugfs_create_file(dbg_files[i].name, dbg_files[i].mode, dbg,
				    wil, dbg_files[i].fops);
}

/* interrupt control blocks */
static const struct {
	const char *name;
	u32 icr_off;
} dbg_icr[] = {
	{"USER_ICR",		HOSTADDR(RGF_USER_USER_ICR)},
	{"DMA_EP_TX_ICR",	HOSTADDR(RGF_DMA_EP_TX_ICR)},
	{"DMA_EP_RX_ICR",	HOSTADDR(RGF_DMA_EP_RX_ICR)},
	{"DMA_EP_MISC_ICR",	HOSTADDR(RGF_DMA_EP_MISC_ICR)},
};

static void wil6210_debugfs_init_isr(struct wil6210_priv *wil,
				     struct dentry *dbg)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(dbg_icr); i++)
		wil6210_debugfs_create_ISR(wil, dbg_icr[i].name, dbg,
					   dbg_icr[i].icr_off);
}

#define WIL_FIELD(name, mode, type) { __stringify(name), mode, \
	offsetof(struct wil6210_priv, name), type}

/* fields in struct wil6210_priv */
static const struct dbg_off dbg_wil_off[] = {
	WIL_FIELD(status[0],	0644,	doff_ulong),
	WIL_FIELD(hw_version,	0444,	doff_x32),
	WIL_FIELD(recovery_count, 0444,	doff_u32),
	WIL_FIELD(discovery_mode, 0644,	doff_u8),
	WIL_FIELD(chip_revision, 0444,	doff_u8),
	WIL_FIELD(abft_len, 0644,		doff_u8),
	WIL_FIELD(wakeup_trigger, 0644,		doff_u8),
	WIL_FIELD(ring_idle_trsh, 0644,	doff_u32),
	WIL_FIELD(num_rx_status_rings, 0644,	doff_u8),
	WIL_FIELD(rx_status_ring_order, 0644,	doff_u32),
	WIL_FIELD(tx_status_ring_order, 0644,	doff_u32),
	WIL_FIELD(rx_buff_id_count, 0644,	doff_u32),
	WIL_FIELD(amsdu_en, 0644,	doff_u8),
	{},
};

static const struct dbg_off dbg_wil_regs[] = {
	{"RGF_MAC_MTRL_COUNTER_0", 0444, HOSTADDR(RGF_MAC_MTRL_COUNTER_0),
		doff_io32},
	{"RGF_USER_USAGE_1", 0444, HOSTADDR(RGF_USER_USAGE_1), doff_io32},
	{"RGF_USER_USAGE_2", 0444, HOSTADDR(RGF_USER_USAGE_2), doff_io32},
	{},
};

/* static parameters */
static const struct dbg_off dbg_statics[] = {
	{"desc_index",	0644, (ulong)&dbg_txdesc_index, doff_u32},
	{"ring_index",	0644, (ulong)&dbg_ring_index, doff_u32},
	{"mem_addr",	0644, (ulong)&mem_addr, doff_u32},
	{"led_polarity", 0644, (ulong)&led_polarity, doff_u8},
	{"status_index", 0644, (ulong)&dbg_status_msg_index, doff_u32},
	{"sring_index",	0644, (ulong)&dbg_sring_index, doff_u32},
	{"drop_if_ring_full", 0644, (ulong)&drop_if_ring_full, doff_u8},
	{},
};

static const int dbg_off_count = 4 * (ARRAY_SIZE(isr_off) - 1) +
				ARRAY_SIZE(dbg_wil_regs) - 1 +
				ARRAY_SIZE(pseudo_isr_off) - 1 +
				ARRAY_SIZE(lgc_itr_cnt_off) - 1 +
				ARRAY_SIZE(tx_itr_cnt_off) - 1 +
				ARRAY_SIZE(rx_itr_cnt_off) - 1;

int wil6210_debugfs_init(struct wil6210_priv *wil)
{
	struct dentry *dbg = wil->debug = debugfs_create_dir(WIL_NAME,
			wil_to_wiphy(wil)->debugfsdir);
	if (IS_ERR_OR_NULL(dbg))
		return -ENODEV;

	wil->dbg_data.data_arr = kcalloc(dbg_off_count,
					 sizeof(struct wil_debugfs_iomem_data),
					 GFP_KERNEL);
	if (!wil->dbg_data.data_arr) {
		debugfs_remove_recursive(dbg);
		wil->debug = NULL;
		return -ENOMEM;
	}

	wil->dbg_data.iomem_data_count = 0;

	wil_pmc_init(wil);

	wil6210_debugfs_init_files(wil, dbg);
	wil6210_debugfs_init_isr(wil, dbg);
	wil6210_debugfs_init_blobs(wil, dbg);
	wil6210_debugfs_init_offset(wil, dbg, wil, dbg_wil_off);
	wil6210_debugfs_init_offset(wil, dbg, (void * __force)wil->csr,
				    dbg_wil_regs);
	wil6210_debugfs_init_offset(wil, dbg, NULL, dbg_statics);

	wil6210_debugfs_create_pseudo_ISR(wil, dbg);

	wil6210_debugfs_create_ITR_CNT(wil, dbg);

	return 0;
}

void wil6210_debugfs_remove(struct wil6210_priv *wil)
{
	int i;

	debugfs_remove_recursive(wil->debug);
	wil->debug = NULL;

	kfree(wil->dbg_data.data_arr);
	for (i = 0; i < wil->max_assoc_sta; i++)
		kfree(wil->sta[i].tx_latency_bins);

	/* free pmc memory without sending command to fw, as it will
	 * be reset on the way down anyway
	 */
	wil_pmc_free(wil, false);
}
