/*
 * Copyright (c) 2012-2016 Qualcomm Atheros, Inc.
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
static u32 dbg_vring_index; /* 24+ for Rx, 0..23 for Tx */
u32 vring_idle_trsh = 16; /* HW fetches up to 16 descriptors at once */

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

static void wil_print_vring(struct seq_file *s, struct wil6210_priv *wil,
			    const char *name, struct vring *vring,
			    char _s, char _h)
{
	void __iomem *x = wmi_addr(wil, vring->hwtail);
	u32 v;

	seq_printf(s, "VRING %s = {\n", name);
	seq_printf(s, "  pa     = %pad\n", &vring->pa);
	seq_printf(s, "  va     = 0x%p\n", vring->va);
	seq_printf(s, "  size   = %d\n", vring->size);
	seq_printf(s, "  swtail = %d\n", vring->swtail);
	seq_printf(s, "  swhead = %d\n", vring->swhead);
	seq_printf(s, "  hwtail = [0x%08x] -> ", vring->hwtail);
	if (x) {
		v = readl(x);
		seq_printf(s, "0x%08x = %d\n", v, v);
	} else {
		seq_puts(s, "???\n");
	}

	if (vring->va && (vring->size <= (1 << WIL_RING_SIZE_ORDER_MAX))) {
		uint i;

		for (i = 0; i < vring->size; i++) {
			volatile struct vring_tx_desc *d = &vring->va[i].tx;

			if ((i % 128) == 0 && (i != 0))
				seq_puts(s, "\n");
			seq_printf(s, "%c", (d->dma.status & BIT(0)) ?
					_s : (vring->ctx[i].skb ? _h : 'h'));
		}
		seq_puts(s, "\n");
	}
	seq_puts(s, "}\n");
}

static int wil_vring_debugfs_show(struct seq_file *s, void *data)
{
	uint i;
	struct wil6210_priv *wil = s->private;

	wil_print_vring(s, wil, "rx", &wil->vring_rx, 'S', '_');

	for (i = 0; i < ARRAY_SIZE(wil->vring_tx); i++) {
		struct vring *vring = &wil->vring_tx[i];
		struct vring_tx_data *txdata = &wil->vring_tx_data[i];

		if (vring->va) {
			int cid = wil->vring2cid_tid[i][0];
			int tid = wil->vring2cid_tid[i][1];
			u32 swhead = vring->swhead;
			u32 swtail = vring->swtail;
			int used = (vring->size + swhead - swtail)
				   % vring->size;
			int avail = vring->size - used - 1;
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

			if (cid < WIL6210_MAX_CID)
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

			wil_print_vring(s, wil, name, vring, '_', 'H');
		}
	}

	return 0;
}

static int wil_vring_seq_open(struct inode *inode, struct file *file)
{
	return single_open(file, wil_vring_debugfs_show, inode->i_private);
}

static const struct file_operations fops_vring = {
	.open		= wil_vring_seq_open,
	.release	= single_release,
	.read		= seq_read,
	.llseek		= seq_lseek,
};

static void wil_seq_hexdump(struct seq_file *s, void *p, int len,
			    const char *prefix)
{
	seq_hex_dump(s, prefix, DUMP_PREFIX_NONE, 16, 1, p, len, false);
}

static void wil_print_ring(struct seq_file *s, const char *prefix,
			   void __iomem *off)
{
	struct wil6210_priv *wil = s->private;
	struct wil6210_mbox_ring r;
	int rsize;
	uint i;

	wil_halp_vote(wil);

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
	wil_halp_unvote(wil);
}

static int wil_mbox_debugfs_show(struct seq_file *s, void *data)
{
	struct wil6210_priv *wil = s->private;

	wil_print_ring(s, "tx", wil->csr + HOST_MBOX +
		       offsetof(struct wil6210_mbox_ctl, tx));
	wil_print_ring(s, "rx", wil->csr + HOST_MBOX +
		       offsetof(struct wil6210_mbox_ctl, rx));

	return 0;
}

static int wil_mbox_seq_open(struct inode *inode, struct file *file)
{
	return single_open(file, wil_mbox_debugfs_show, inode->i_private);
}

static const struct file_operations fops_mbox = {
	.open		= wil_mbox_seq_open,
	.release	= single_release,
	.read		= seq_read,
	.llseek		= seq_lseek,
};

static int wil_debugfs_iomem_x32_set(void *data, u64 val)
{
	writel(val, (void __iomem *)data);
	wmb(); /* make sure write propagated to HW */

	return 0;
}

static int wil_debugfs_iomem_x32_get(void *data, u64 *val)
{
	*val = readl((void __iomem *)data);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(fops_iomem_x32, wil_debugfs_iomem_x32_get,
			wil_debugfs_iomem_x32_set, "0x%08llx\n");

static struct dentry *wil_debugfs_create_iomem_x32(const char *name,
						   umode_t mode,
						   struct dentry *parent,
						   void *value)
{
	return debugfs_create_file(name, mode, parent, value,
				   &fops_iomem_x32);
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

DEFINE_SIMPLE_ATTRIBUTE(wil_fops_ulong, wil_debugfs_ulong_get,
			wil_debugfs_ulong_set, "0x%llx\n");

static struct dentry *wil_debugfs_create_ulong(const char *name, umode_t mode,
					       struct dentry *parent,
					       ulong *value)
{
	return debugfs_create_file(name, mode, parent, value, &wil_fops_ulong);
}

/**
 * wil6210_debugfs_init_offset - create set of debugfs files
 * @wil - driver's context, used for printing
 * @dbg - directory on the debugfs, where files will be created
 * @base - base address used in address calculation
 * @tbl - table with file descriptions. Should be terminated with empty element.
 *
 * Creates files accordingly to the @tbl.
 */
static void wil6210_debugfs_init_offset(struct wil6210_priv *wil,
					struct dentry *dbg, void *base,
					const struct dbg_off * const tbl)
{
	int i;

	for (i = 0; tbl[i].name; i++) {
		struct dentry *f;

		switch (tbl[i].type) {
		case doff_u32:
			f = debugfs_create_u32(tbl[i].name, tbl[i].mode, dbg,
					       base + tbl[i].off);
			break;
		case doff_x32:
			f = debugfs_create_x32(tbl[i].name, tbl[i].mode, dbg,
					       base + tbl[i].off);
			break;
		case doff_ulong:
			f = wil_debugfs_create_ulong(tbl[i].name, tbl[i].mode,
						     dbg, base + tbl[i].off);
			break;
		case doff_io32:
			f = wil_debugfs_create_iomem_x32(tbl[i].name,
							 tbl[i].mode, dbg,
							 base + tbl[i].off);
			break;
		case doff_u8:
			f = debugfs_create_u8(tbl[i].name, tbl[i].mode, dbg,
					      base + tbl[i].off);
			break;
		default:
			f = ERR_PTR(-EINVAL);
		}
		if (IS_ERR_OR_NULL(f))
			wil_err(wil, "Create file \"%s\": err %ld\n",
				tbl[i].name, PTR_ERR(f));
	}
}

static const struct dbg_off isr_off[] = {
	{"ICC", S_IRUGO | S_IWUSR, offsetof(struct RGF_ICR, ICC), doff_io32},
	{"ICR", S_IRUGO | S_IWUSR, offsetof(struct RGF_ICR, ICR), doff_io32},
	{"ICM", S_IRUGO | S_IWUSR, offsetof(struct RGF_ICR, ICM), doff_io32},
	{"ICS",		  S_IWUSR, offsetof(struct RGF_ICR, ICS), doff_io32},
	{"IMV", S_IRUGO | S_IWUSR, offsetof(struct RGF_ICR, IMV), doff_io32},
	{"IMS",		  S_IWUSR, offsetof(struct RGF_ICR, IMS), doff_io32},
	{"IMC",		  S_IWUSR, offsetof(struct RGF_ICR, IMC), doff_io32},
	{},
};

static int wil6210_debugfs_create_ISR(struct wil6210_priv *wil,
				      const char *name,
				      struct dentry *parent, u32 off)
{
	struct dentry *d = debugfs_create_dir(name, parent);

	if (IS_ERR_OR_NULL(d))
		return -ENODEV;

	wil6210_debugfs_init_offset(wil, d, (void * __force)wil->csr + off,
				    isr_off);

	return 0;
}

static const struct dbg_off pseudo_isr_off[] = {
	{"CAUSE",   S_IRUGO, HOSTADDR(RGF_DMA_PSEUDO_CAUSE), doff_io32},
	{"MASK_SW", S_IRUGO, HOSTADDR(RGF_DMA_PSEUDO_CAUSE_MASK_SW), doff_io32},
	{"MASK_FW", S_IRUGO, HOSTADDR(RGF_DMA_PSEUDO_CAUSE_MASK_FW), doff_io32},
	{},
};

static int wil6210_debugfs_create_pseudo_ISR(struct wil6210_priv *wil,
					     struct dentry *parent)
{
	struct dentry *d = debugfs_create_dir("PSEUDO_ISR", parent);

	if (IS_ERR_OR_NULL(d))
		return -ENODEV;

	wil6210_debugfs_init_offset(wil, d, (void * __force)wil->csr,
				    pseudo_isr_off);

	return 0;
}

static const struct dbg_off lgc_itr_cnt_off[] = {
	{"TRSH", S_IRUGO | S_IWUSR, HOSTADDR(RGF_DMA_ITR_CNT_TRSH), doff_io32},
	{"DATA", S_IRUGO | S_IWUSR, HOSTADDR(RGF_DMA_ITR_CNT_DATA), doff_io32},
	{"CTL",  S_IRUGO | S_IWUSR, HOSTADDR(RGF_DMA_ITR_CNT_CRL), doff_io32},
	{},
};

static const struct dbg_off tx_itr_cnt_off[] = {
	{"TRSH", S_IRUGO | S_IWUSR, HOSTADDR(RGF_DMA_ITR_TX_CNT_TRSH),
	 doff_io32},
	{"DATA", S_IRUGO | S_IWUSR, HOSTADDR(RGF_DMA_ITR_TX_CNT_DATA),
	 doff_io32},
	{"CTL",  S_IRUGO | S_IWUSR, HOSTADDR(RGF_DMA_ITR_TX_CNT_CTL),
	 doff_io32},
	{"IDL_TRSH", S_IRUGO | S_IWUSR, HOSTADDR(RGF_DMA_ITR_TX_IDL_CNT_TRSH),
	 doff_io32},
	{"IDL_DATA", S_IRUGO | S_IWUSR, HOSTADDR(RGF_DMA_ITR_TX_IDL_CNT_DATA),
	 doff_io32},
	{"IDL_CTL",  S_IRUGO | S_IWUSR, HOSTADDR(RGF_DMA_ITR_TX_IDL_CNT_CTL),
	 doff_io32},
	{},
};

static const struct dbg_off rx_itr_cnt_off[] = {
	{"TRSH", S_IRUGO | S_IWUSR, HOSTADDR(RGF_DMA_ITR_RX_CNT_TRSH),
	 doff_io32},
	{"DATA", S_IRUGO | S_IWUSR, HOSTADDR(RGF_DMA_ITR_RX_CNT_DATA),
	 doff_io32},
	{"CTL",  S_IRUGO | S_IWUSR, HOSTADDR(RGF_DMA_ITR_RX_CNT_CTL),
	 doff_io32},
	{"IDL_TRSH", S_IRUGO | S_IWUSR, HOSTADDR(RGF_DMA_ITR_RX_IDL_CNT_TRSH),
	 doff_io32},
	{"IDL_DATA", S_IRUGO | S_IWUSR, HOSTADDR(RGF_DMA_ITR_RX_IDL_CNT_DATA),
	 doff_io32},
	{"IDL_CTL",  S_IRUGO | S_IWUSR, HOSTADDR(RGF_DMA_ITR_RX_IDL_CNT_CTL),
	 doff_io32},
	{},
};

static int wil6210_debugfs_create_ITR_CNT(struct wil6210_priv *wil,
					  struct dentry *parent)
{
	struct dentry *d, *dtx, *drx;

	d = debugfs_create_dir("ITR_CNT", parent);
	if (IS_ERR_OR_NULL(d))
		return -ENODEV;

	dtx = debugfs_create_dir("TX", d);
	drx = debugfs_create_dir("RX", d);
	if (IS_ERR_OR_NULL(dtx) || IS_ERR_OR_NULL(drx))
		return -ENODEV;

	wil6210_debugfs_init_offset(wil, d, (void * __force)wil->csr,
				    lgc_itr_cnt_off);

	wil6210_debugfs_init_offset(wil, dtx, (void * __force)wil->csr,
				    tx_itr_cnt_off);

	wil6210_debugfs_init_offset(wil, drx, (void * __force)wil->csr,
				    rx_itr_cnt_off);
	return 0;
}

static int wil_memread_debugfs_show(struct seq_file *s, void *data)
{
	struct wil6210_priv *wil = s->private;
	void __iomem *a = wmi_buffer(wil, cpu_to_le32(mem_addr));

	if (a)
		seq_printf(s, "[0x%08x] = 0x%08x\n", mem_addr, readl(a));
	else
		seq_printf(s, "[0x%08x] = INVALID\n", mem_addr);

	return 0;
}

static int wil_memread_seq_open(struct inode *inode, struct file *file)
{
	return single_open(file, wil_memread_debugfs_show, inode->i_private);
}

static const struct file_operations fops_memread = {
	.open		= wil_memread_seq_open,
	.release	= single_release,
	.read		= seq_read,
	.llseek		= seq_lseek,
};

static ssize_t wil_read_file_ioblob(struct file *file, char __user *user_buf,
				    size_t count, loff_t *ppos)
{
	enum { max_count = 4096 };
	struct wil_blob_wrapper *wil_blob = file->private_data;
	loff_t pos = *ppos;
	size_t available = wil_blob->blob.size;
	void *buf;
	size_t ret;

	if (pos < 0)
		return -EINVAL;

	if (pos >= available || !count)
		return 0;

	if (count > available - pos)
		count = available - pos;
	if (count > max_count)
		count = max_count;

	buf = kmalloc(count, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	wil_memcpy_fromio_halp_vote(wil_blob->wil, buf,
				    (const volatile void __iomem *)
				    wil_blob->blob.data + pos, count);

	ret = copy_to_user(user_buf, buf, count);
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

/*---reset---*/
static ssize_t wil_write_file_reset(struct file *file, const char __user *buf,
				    size_t len, loff_t *ppos)
{
	struct wil6210_priv *wil = file->private_data;
	struct net_device *ndev = wil_to_ndev(wil);

	/**
	 * BUG:
	 * this code does NOT sync device state with the rest of system
	 * use with care, debug only!!!
	 */
	rtnl_lock();
	dev_close(ndev);
	ndev->flags &= ~IFF_UP;
	rtnl_unlock();
	wil_reset(wil, true);

	return len;
}

static const struct file_operations fops_reset = {
	.write = wil_write_file_reset,
	.open  = simple_open,
};

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

	if (0 == strcmp(cmd, "add")) {
		if (rc < 3) {
			wil_err(wil, "BACK: add require at least 2 params\n");
			return -EINVAL;
		}
		if (rc < 4)
			p3 = 0;
		wmi_addba(wil, p1, p2, p3);
	} else if (0 == strcmp(cmd, "del_tx")) {
		if (rc < 3)
			p2 = WLAN_REASON_QSTA_LEAVE_QBSS;
		wmi_delba_tx(wil, p1, p2);
	} else if (0 == strcmp(cmd, "del_rx")) {
		if (rc < 3) {
			wil_err(wil,
				"BACK: del_rx require at least 2 params\n");
			return -EINVAL;
		}
		if (rc < 4)
			p3 = WLAN_REASON_QSTA_LEAVE_QBSS;
		wmi_delba_rx(wil, mk_cidxtid(p1, p2), p3);
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

	sprintf(text, "Last command status: %d\n\n%s",
		wil_pmc_last_cmd_status(wil),
		help);

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

/*---tx_mgmt---*/
/* Write mgmt frame to this file to send it */
static ssize_t wil_write_file_txmgmt(struct file *file, const char __user *buf,
				     size_t len, loff_t *ppos)
{
	struct wil6210_priv *wil = file->private_data;
	struct wiphy *wiphy = wil_to_wiphy(wil);
	struct wireless_dev *wdev = wil_to_wdev(wil);
	struct cfg80211_mgmt_tx_params params;
	int rc;
	void *frame = kmalloc(len, GFP_KERNEL);

	if (!frame)
		return -ENOMEM;

	if (copy_from_user(frame, buf, len)) {
		kfree(frame);
		return -EIO;
	}

	params.buf = frame;
	params.len = len;
	params.chan = wdev->preset_chandef.chan;

	rc = wil_cfg80211_mgmt_tx(wiphy, wdev, &params, NULL);

	kfree(frame);
	wil_info(wil, "%s() -> %d\n", __func__, rc);

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
	struct wmi_cmd_hdr *wmi;
	void *cmd;
	int cmdlen = len - sizeof(struct wmi_cmd_hdr);
	u16 cmdid;
	int rc, rc1;

	if (cmdlen < 0)
		return -EINVAL;

	wmi = kmalloc(len, GFP_KERNEL);
	if (!wmi)
		return -ENOMEM;

	rc = simple_write_to_buffer(wmi, len, ppos, buf, len);
	if (rc < 0) {
		kfree(wmi);
		return rc;
	}

	cmd = (cmdlen > 0) ? &wmi[1] : NULL;
	cmdid = le16_to_cpu(wmi->command_id);

	rc1 = wmi_send(wil, cmdid, cmd, cmdlen);
	kfree(wmi);

	wil_info(wil, "%s(0x%04x[%d]) -> %d\n", __func__, cmdid, cmdlen, rc1);

	return rc;
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
			const struct skb_frag_struct *frag =
					&skb_shinfo(skb)->frags[i];

			len = skb_frag_size(frag);
			p = skb_frag_address_safe(frag);
			seq_printf(s, "    [%2d] : len = %d\n", i, len);
			wil_seq_hexdump(s, p, len, "      : ");
		}
	}
}

/*---------Tx/Rx descriptor------------*/
static int wil_txdesc_debugfs_show(struct seq_file *s, void *data)
{
	struct wil6210_priv *wil = s->private;
	struct vring *vring;
	bool tx = (dbg_vring_index < WIL6210_MAX_TX_RINGS);

	vring = tx ? &wil->vring_tx[dbg_vring_index] : &wil->vring_rx;

	if (!vring->va) {
		if (tx)
			seq_printf(s, "No Tx[%2d] VRING\n", dbg_vring_index);
		else
			seq_puts(s, "No Rx VRING\n");
		return 0;
	}

	if (dbg_txdesc_index < vring->size) {
		/* use struct vring_tx_desc for Rx as well,
		 * only field used, .dma.length, is the same
		 */
		volatile struct vring_tx_desc *d =
				&vring->va[dbg_txdesc_index].tx;
		volatile u32 *u = (volatile u32 *)d;
		struct sk_buff *skb = vring->ctx[dbg_txdesc_index].skb;

		if (tx)
			seq_printf(s, "Tx[%2d][%3d] = {\n", dbg_vring_index,
				   dbg_txdesc_index);
		else
			seq_printf(s, "Rx[%3d] = {\n", dbg_txdesc_index);
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
	} else {
		if (tx)
			seq_printf(s, "[%2d] TxDesc index (%d) >= size (%d)\n",
				   dbg_vring_index, dbg_txdesc_index,
				   vring->size);
		else
			seq_printf(s, "RxDesc index (%d) >= size (%d)\n",
				   dbg_txdesc_index, vring->size);
	}

	return 0;
}

static int wil_txdesc_seq_open(struct inode *inode, struct file *file)
{
	return single_open(file, wil_txdesc_debugfs_show, inode->i_private);
}

static const struct file_operations fops_txdesc = {
	.open		= wil_txdesc_seq_open,
	.release	= single_release,
	.read		= seq_read,
	.llseek		= seq_lseek,
};

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

static int wil_bf_debugfs_show(struct seq_file *s, void *data)
{
	int rc;
	int i;
	struct wil6210_priv *wil = s->private;
	struct wmi_notify_req_cmd cmd = {
		.interval_usec = 0,
	};
	struct {
		struct wmi_cmd_hdr wmi;
		struct wmi_notify_req_done_event evt;
	} __packed reply;

	for (i = 0; i < ARRAY_SIZE(wil->sta); i++) {
		u32 status;

		cmd.cid = i;
		rc = wmi_call(wil, WMI_NOTIFY_REQ_CMDID, &cmd, sizeof(cmd),
			      WMI_NOTIFY_REQ_DONE_EVENTID, &reply,
			      sizeof(reply), 20);
		/* if reply is all-0, ignore this CID */
		if (rc || is_all_zeros(&reply.evt, sizeof(reply.evt)))
			continue;

		status = le32_to_cpu(reply.evt.status);
		seq_printf(s, "CID %d {\n"
			   "  TSF = 0x%016llx\n"
			   "  TxMCS = %2d TxTpt = %4d\n"
			   "  SQI = %4d\n"
			   "  Status = 0x%08x %s\n"
			   "  Sectors(rx:tx) my %2d:%2d peer %2d:%2d\n"
			   "  Goodput(rx:tx) %4d:%4d\n"
			   "}\n",
			   i,
			   le64_to_cpu(reply.evt.tsf),
			   le16_to_cpu(reply.evt.bf_mcs),
			   le32_to_cpu(reply.evt.tx_tpt),
			   reply.evt.sqi,
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

static int wil_bf_seq_open(struct inode *inode, struct file *file)
{
	return single_open(file, wil_bf_debugfs_show, inode->i_private);
}

static const struct file_operations fops_bf = {
	.open		= wil_bf_seq_open,
	.release	= single_release,
	.read		= seq_read,
	.llseek		= seq_lseek,
};

/*---------SSID------------*/
static ssize_t wil_read_file_ssid(struct file *file, char __user *user_buf,
				  size_t count, loff_t *ppos)
{
	struct wil6210_priv *wil = file->private_data;
	struct wireless_dev *wdev = wil_to_wdev(wil);

	return simple_read_from_buffer(user_buf, count, ppos,
				       wdev->ssid, wdev->ssid_len);
}

static ssize_t wil_write_file_ssid(struct file *file, const char __user *buf,
				   size_t count, loff_t *ppos)
{
	struct wil6210_priv *wil = file->private_data;
	struct wireless_dev *wdev = wil_to_wdev(wil);
	struct net_device *ndev = wil_to_ndev(wil);

	if (*ppos != 0) {
		wil_err(wil, "Unable to set SSID substring from [%d]\n",
			(int)*ppos);
		return -EINVAL;
	}

	if (count > sizeof(wdev->ssid)) {
		wil_err(wil, "SSID too long, len = %d\n", (int)count);
		return -EINVAL;
	}
	if (netif_running(ndev)) {
		wil_err(wil, "Unable to change SSID on running interface\n");
		return -EINVAL;
	}

	wdev->ssid_len = count;
	return simple_write_to_buffer(wdev->ssid, wdev->ssid_len, ppos,
				      buf, count);
}

static const struct file_operations fops_ssid = {
	.read = wil_read_file_ssid,
	.write = wil_write_file_ssid,
	.open  = simple_open,
};

/*---------temp------------*/
static void print_temp(struct seq_file *s, const char *prefix, u32 t)
{
	switch (t) {
	case 0:
	case ~(u32)0:
		seq_printf(s, "%s N/A\n", prefix);
	break;
	default:
		seq_printf(s, "%s %d.%03d\n", prefix, t / 1000, t % 1000);
		break;
	}
}

static int wil_temp_debugfs_show(struct seq_file *s, void *data)
{
	struct wil6210_priv *wil = s->private;
	u32 t_m, t_r;
	int rc = wmi_get_temperature(wil, &t_m, &t_r);

	if (rc) {
		seq_puts(s, "Failed\n");
		return 0;
	}

	print_temp(s, "T_mac   =", t_m);
	print_temp(s, "T_radio =", t_r);

	return 0;
}

static int wil_temp_seq_open(struct inode *inode, struct file *file)
{
	return single_open(file, wil_temp_debugfs_show, inode->i_private);
}

static const struct file_operations fops_temp = {
	.open		= wil_temp_seq_open,
	.release	= single_release,
	.read		= seq_read,
	.llseek		= seq_lseek,
};

/*---------freq------------*/
static int wil_freq_debugfs_show(struct seq_file *s, void *data)
{
	struct wil6210_priv *wil = s->private;
	struct wireless_dev *wdev = wil_to_wdev(wil);
	u16 freq = wdev->chandef.chan ? wdev->chandef.chan->center_freq : 0;

	seq_printf(s, "Freq = %d\n", freq);

	return 0;
}

static int wil_freq_seq_open(struct inode *inode, struct file *file)
{
	return single_open(file, wil_freq_debugfs_show, inode->i_private);
}

static const struct file_operations fops_freq = {
	.open		= wil_freq_seq_open,
	.release	= single_release,
	.read		= seq_read,
	.llseek		= seq_lseek,
};

/*---------link------------*/
static int wil_link_debugfs_show(struct seq_file *s, void *data)
{
	struct wil6210_priv *wil = s->private;
	struct station_info sinfo;
	int i, rc;

	for (i = 0; i < ARRAY_SIZE(wil->sta); i++) {
		struct wil_sta_info *p = &wil->sta[i];
		char *status = "unknown";

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
		seq_printf(s, "[%d] %pM %s\n", i, p->addr, status);

		if (p->status == wil_sta_connected) {
			rc = wil_cid_fill_sinfo(wil, i, &sinfo);
			if (rc)
				return rc;

			seq_printf(s, "  Tx_mcs = %d\n", sinfo.txrate.mcs);
			seq_printf(s, "  Rx_mcs = %d\n", sinfo.rxrate.mcs);
			seq_printf(s, "  SQ     = %d\n", sinfo.signal);
		}
	}

	return 0;
}

static int wil_link_seq_open(struct inode *inode, struct file *file)
{
	return single_open(file, wil_link_debugfs_show, inode->i_private);
}

static const struct file_operations fops_link = {
	.open		= wil_link_seq_open,
	.release	= single_release,
	.read		= seq_read,
	.llseek		= seq_lseek,
};

/*---------info------------*/
static int wil_info_debugfs_show(struct seq_file *s, void *data)
{
	struct wil6210_priv *wil = s->private;
	struct net_device *ndev = wil_to_ndev(wil);
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

static int wil_info_seq_open(struct inode *inode, struct file *file)
{
	return single_open(file, wil_info_debugfs_show, inode->i_private);
}

static const struct file_operations fops_info = {
	.open		= wil_info_seq_open,
	.release	= single_release,
	.read		= seq_read,
	.llseek		= seq_lseek,
};

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

	seq_printf(s, "([%2d] %3d TU) 0x%03x [", r->buf_size, r->timeout,
		   r->head_seq_num);
	for (i = 0; i < r->buf_size; i++) {
		if (i == index)
			seq_printf(s, "%c", r->reorder_buf[i] ? 'O' : '|');
		else
			seq_printf(s, "%c", r->reorder_buf[i] ? '*' : '_');
	}
	seq_printf(s,
		   "] total %llu drop %llu (dup %llu + old %llu) last 0x%03x\n",
		   r->total, drop_dup + drop_old, drop_dup, drop_old,
		   r->ssn_last_drop);
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

static int wil_sta_debugfs_show(struct seq_file *s, void *data)
__acquires(&p->tid_rx_lock) __releases(&p->tid_rx_lock)
{
	struct wil6210_priv *wil = s->private;
	int i, tid, mcs;

	for (i = 0; i < ARRAY_SIZE(wil->sta); i++) {
		struct wil_sta_info *p = &wil->sta[i];
		char *status = "unknown";

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
		seq_printf(s, "[%d] %pM %s\n", i, p->addr, status);

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

static int wil_sta_seq_open(struct inode *inode, struct file *file)
{
	return single_open(file, wil_sta_debugfs_show, inode->i_private);
}

static const struct file_operations fops_sta = {
	.open		= wil_sta_seq_open,
	.release	= single_release,
	.read		= seq_read,
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
static int wil_fw_capabilities_debugfs_show(struct seq_file *s, void *data)
{
	struct wil6210_priv *wil = s->private;

	seq_printf(s, "fw_capabilities : %*pb\n", WMI_FW_CAPABILITY_MAX,
		   wil->fw_capabilities);

	return 0;
}

static int wil_fw_capabilities_seq_open(struct inode *inode, struct file *file)
{
	return single_open(file, wil_fw_capabilities_debugfs_show,
			   inode->i_private);
}

static const struct file_operations fops_fw_capabilities = {
	.open		= wil_fw_capabilities_seq_open,
	.release	= single_release,
	.read		= seq_read,
	.llseek		= seq_lseek,
};

/*---------FW version------------*/
static int wil_fw_version_debugfs_show(struct seq_file *s, void *data)
{
	struct wil6210_priv *wil = s->private;

	if (wil->fw_version[0])
		seq_printf(s, "%s\n", wil->fw_version);
	else
		seq_puts(s, "N/A\n");

	return 0;
}

static int wil_fw_version_seq_open(struct inode *inode, struct file *file)
{
	return single_open(file, wil_fw_version_debugfs_show,
			   inode->i_private);
}

static const struct file_operations fops_fw_version = {
	.open		= wil_fw_version_seq_open,
	.release	= single_release,
	.read		= seq_read,
	.llseek		= seq_lseek,
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
		wil_debugfs_create_ioblob(name, S_IRUGO, dbg, wil_blob);
	}
}

/* misc files */
static const struct {
	const char *name;
	umode_t mode;
	const struct file_operations *fops;
} dbg_files[] = {
	{"mbox",	S_IRUGO,		&fops_mbox},
	{"vrings",	S_IRUGO,		&fops_vring},
	{"stations",	S_IRUGO,		&fops_sta},
	{"desc",	S_IRUGO,		&fops_txdesc},
	{"bf",		S_IRUGO,		&fops_bf},
	{"ssid",	S_IRUGO | S_IWUSR,	&fops_ssid},
	{"mem_val",	S_IRUGO,		&fops_memread},
	{"reset",		  S_IWUSR,	&fops_reset},
	{"rxon",		  S_IWUSR,	&fops_rxon},
	{"tx_mgmt",		  S_IWUSR,	&fops_txmgmt},
	{"wmi_send",		  S_IWUSR,	&fops_wmi},
	{"back",	S_IRUGO | S_IWUSR,	&fops_back},
	{"pmccfg",	S_IRUGO | S_IWUSR,	&fops_pmccfg},
	{"pmcdata",	S_IRUGO,		&fops_pmcdata},
	{"temp",	S_IRUGO,		&fops_temp},
	{"freq",	S_IRUGO,		&fops_freq},
	{"link",	S_IRUGO,		&fops_link},
	{"info",	S_IRUGO,		&fops_info},
	{"recovery",	S_IRUGO | S_IWUSR,	&fops_recovery},
	{"led_cfg",	S_IRUGO | S_IWUSR,	&fops_led_cfg},
	{"led_blink_time",	S_IRUGO | S_IWUSR,	&fops_led_blink_time},
	{"fw_capabilities",	S_IRUGO,	&fops_fw_capabilities},
	{"fw_version",	S_IRUGO,		&fops_fw_version},
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
	WIL_FIELD(privacy,	S_IRUGO,		doff_u32),
	WIL_FIELD(status[0],	S_IRUGO | S_IWUSR,	doff_ulong),
	WIL_FIELD(hw_version,	S_IRUGO,		doff_x32),
	WIL_FIELD(recovery_count, S_IRUGO,		doff_u32),
	WIL_FIELD(ap_isolate,	S_IRUGO,		doff_u32),
	WIL_FIELD(discovery_mode, S_IRUGO | S_IWUSR,	doff_u8),
	{},
};

static const struct dbg_off dbg_wil_regs[] = {
	{"RGF_MAC_MTRL_COUNTER_0", S_IRUGO, HOSTADDR(RGF_MAC_MTRL_COUNTER_0),
		doff_io32},
	{"RGF_USER_USAGE_1", S_IRUGO, HOSTADDR(RGF_USER_USAGE_1), doff_io32},
	{},
};

/* static parameters */
static const struct dbg_off dbg_statics[] = {
	{"desc_index",	S_IRUGO | S_IWUSR, (ulong)&dbg_txdesc_index, doff_u32},
	{"vring_index",	S_IRUGO | S_IWUSR, (ulong)&dbg_vring_index, doff_u32},
	{"mem_addr",	S_IRUGO | S_IWUSR, (ulong)&mem_addr, doff_u32},
	{"vring_idle_trsh", S_IRUGO | S_IWUSR, (ulong)&vring_idle_trsh,
	 doff_u32},
	{"led_polarity", S_IRUGO | S_IWUSR, (ulong)&led_polarity, doff_u8},
	{},
};

int wil6210_debugfs_init(struct wil6210_priv *wil)
{
	struct dentry *dbg = wil->debug = debugfs_create_dir(WIL_NAME,
			wil_to_wiphy(wil)->debugfsdir);

	if (IS_ERR_OR_NULL(dbg))
		return -ENODEV;

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
	debugfs_remove_recursive(wil->debug);
	wil->debug = NULL;

	/* free pmc memory without sending command to fw, as it will
	 * be reset on the way down anyway
	 */
	wil_pmc_free(wil, false);
}
