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

#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/pci.h>
#include <linux/rtnetlink.h>

#include "wil6210.h"
#include "txrx.h"

/* Nasty hack. Better have per device instances */
static u32 mem_addr;
static u32 dbg_txdesc_index;

static void wil_print_vring(struct seq_file *s, struct wil6210_priv *wil,
			    const char *name, struct vring *vring)
{
	void __iomem *x = wmi_addr(wil, vring->hwtail);

	seq_printf(s, "VRING %s = {\n", name);
	seq_printf(s, "  pa     = 0x%016llx\n", (unsigned long long)vring->pa);
	seq_printf(s, "  va     = 0x%p\n", vring->va);
	seq_printf(s, "  size   = %d\n", vring->size);
	seq_printf(s, "  swtail = %d\n", vring->swtail);
	seq_printf(s, "  swhead = %d\n", vring->swhead);
	seq_printf(s, "  hwtail = [0x%08x] -> ", vring->hwtail);
	if (x)
		seq_printf(s, "0x%08x\n", ioread32(x));
	else
		seq_printf(s, "???\n");

	if (vring->va && (vring->size < 1025)) {
		uint i;
		for (i = 0; i < vring->size; i++) {
			volatile struct vring_tx_desc *d = &vring->va[i].tx;
			if ((i % 64) == 0 && (i != 0))
				seq_printf(s, "\n");
			seq_printf(s, "%s", (d->dma.status & BIT(0)) ?
					"S" : (vring->ctx[i].skb ? "H" : "h"));
		}
		seq_printf(s, "\n");
	}
	seq_printf(s, "}\n");
}

static int wil_vring_debugfs_show(struct seq_file *s, void *data)
{
	uint i;
	struct wil6210_priv *wil = s->private;

	wil_print_vring(s, wil, "rx", &wil->vring_rx);

	for (i = 0; i < ARRAY_SIZE(wil->vring_tx); i++) {
		struct vring *vring = &(wil->vring_tx[i]);
		if (vring->va) {
			char name[10];
			snprintf(name, sizeof(name), "tx_%2d", i);
			wil_print_vring(s, wil, name, vring);
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

static void wil_print_ring(struct seq_file *s, const char *prefix,
			   void __iomem *off)
{
	struct wil6210_priv *wil = s->private;
	struct wil6210_mbox_ring r;
	int rsize;
	uint i;

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
		seq_printf(s, "  ??? pointers are garbage?\n");
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
				int n = 0;
				char printbuf[16 * 3 + 2];
				unsigned char databuf[MAX_MBOXITEM_SIZE];
				void __iomem *src = wmi_buffer(wil, d.addr) +
					sizeof(struct wil6210_mbox_hdr);
				/*
				 * No need to check @src for validity -
				 * we already validated @d.addr while
				 * reading header
				 */
				wil_memcpy_fromio_32(databuf, src, len);
				while (n < len) {
					int l = min(len - n, 16);
					hex_dump_to_buffer(databuf + n, l,
							   16, 1, printbuf,
							   sizeof(printbuf),
							   false);
					seq_printf(s, "      : %s\n", printbuf);
					n += l;
				}
			}
		} else {
			seq_printf(s, "\n");
		}
	}
 out:
	seq_printf(s, "}\n");
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
	iowrite32(val, (void __iomem *)data);
	wmb(); /* make sure write propagated to HW */

	return 0;
}

static int wil_debugfs_iomem_x32_get(void *data, u64 *val)
{
	*val = ioread32((void __iomem *)data);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(fops_iomem_x32, wil_debugfs_iomem_x32_get,
			wil_debugfs_iomem_x32_set, "0x%08llx\n");

static struct dentry *wil_debugfs_create_iomem_x32(const char *name,
						   umode_t mode,
						   struct dentry *parent,
						   void __iomem *value)
{
	return debugfs_create_file(name, mode, parent, (void * __force)value,
				   &fops_iomem_x32);
}

static int wil6210_debugfs_create_ISR(struct wil6210_priv *wil,
				      const char *name,
				      struct dentry *parent, u32 off)
{
	struct dentry *d = debugfs_create_dir(name, parent);

	if (IS_ERR_OR_NULL(d))
		return -ENODEV;

	wil_debugfs_create_iomem_x32("ICC", S_IRUGO | S_IWUSR, d,
				     wil->csr + off);
	wil_debugfs_create_iomem_x32("ICR", S_IRUGO | S_IWUSR, d,
				     wil->csr + off + 4);
	wil_debugfs_create_iomem_x32("ICM", S_IRUGO | S_IWUSR, d,
				     wil->csr + off + 8);
	wil_debugfs_create_iomem_x32("ICS", S_IWUSR, d,
				     wil->csr + off + 12);
	wil_debugfs_create_iomem_x32("IMV", S_IRUGO | S_IWUSR, d,
				     wil->csr + off + 16);
	wil_debugfs_create_iomem_x32("IMS", S_IWUSR, d,
				     wil->csr + off + 20);
	wil_debugfs_create_iomem_x32("IMC", S_IWUSR, d,
				     wil->csr + off + 24);

	return 0;
}

static int wil6210_debugfs_create_pseudo_ISR(struct wil6210_priv *wil,
					     struct dentry *parent)
{
	struct dentry *d = debugfs_create_dir("PSEUDO_ISR", parent);

	if (IS_ERR_OR_NULL(d))
		return -ENODEV;

	wil_debugfs_create_iomem_x32("CAUSE", S_IRUGO, d, wil->csr +
				     HOSTADDR(RGF_DMA_PSEUDO_CAUSE));
	wil_debugfs_create_iomem_x32("MASK_SW", S_IRUGO, d, wil->csr +
				     HOSTADDR(RGF_DMA_PSEUDO_CAUSE_MASK_SW));
	wil_debugfs_create_iomem_x32("MASK_FW", S_IRUGO, d, wil->csr +
				     HOSTADDR(RGF_DMA_PSEUDO_CAUSE_MASK_FW));

	return 0;
}

static int wil6210_debugfs_create_ITR_CNT(struct wil6210_priv *wil,
					  struct dentry *parent)
{
	struct dentry *d = debugfs_create_dir("ITR_CNT", parent);

	if (IS_ERR_OR_NULL(d))
		return -ENODEV;

	wil_debugfs_create_iomem_x32("TRSH", S_IRUGO, d, wil->csr +
				     HOSTADDR(RGF_DMA_ITR_CNT_TRSH));
	wil_debugfs_create_iomem_x32("DATA", S_IRUGO, d, wil->csr +
				     HOSTADDR(RGF_DMA_ITR_CNT_DATA));
	wil_debugfs_create_iomem_x32("CTL", S_IRUGO, d, wil->csr +
				     HOSTADDR(RGF_DMA_ITR_CNT_CRL));

	return 0;
}

static int wil_memread_debugfs_show(struct seq_file *s, void *data)
{
	struct wil6210_priv *wil = s->private;
	void __iomem *a = wmi_buffer(wil, cpu_to_le32(mem_addr));

	if (a)
		seq_printf(s, "[0x%08x] = 0x%08x\n", mem_addr, ioread32(a));
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
	struct debugfs_blob_wrapper *blob = file->private_data;
	loff_t pos = *ppos;
	size_t available = blob->size;
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

	wil_memcpy_fromio_32(buf, (const volatile void __iomem *)blob->data +
			     pos, count);

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
					 struct debugfs_blob_wrapper *blob)
{
	return debugfs_create_file(name, mode, parent, blob, &fops_ioblob);
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
	wil_reset(wil);

	return len;
}

static const struct file_operations fops_reset = {
	.write = wil_write_file_reset,
	.open  = simple_open,
};
/*---------Tx descriptor------------*/

static int wil_txdesc_debugfs_show(struct seq_file *s, void *data)
{
	struct wil6210_priv *wil = s->private;
	struct vring *vring = &(wil->vring_tx[0]);

	if (!vring->va) {
		seq_printf(s, "No Tx VRING\n");
		return 0;
	}

	if (dbg_txdesc_index < vring->size) {
		volatile struct vring_tx_desc *d =
				&(vring->va[dbg_txdesc_index].tx);
		volatile u32 *u = (volatile u32 *)d;
		struct sk_buff *skb = vring->ctx[dbg_txdesc_index].skb;

		seq_printf(s, "Tx[%3d] = {\n", dbg_txdesc_index);
		seq_printf(s, "  MAC = 0x%08x 0x%08x 0x%08x 0x%08x\n",
			   u[0], u[1], u[2], u[3]);
		seq_printf(s, "  DMA = 0x%08x 0x%08x 0x%08x 0x%08x\n",
			   u[4], u[5], u[6], u[7]);
		seq_printf(s, "  SKB = %p\n", skb);

		if (skb) {
			char printbuf[16 * 3 + 2];
			int i = 0;
			int len = le16_to_cpu(d->dma.length);
			void *p = skb->data;

			if (len != skb_headlen(skb)) {
				seq_printf(s, "!!! len: desc = %d skb = %d\n",
					   len, skb_headlen(skb));
				len = min_t(int, len, skb_headlen(skb));
			}

			seq_printf(s, "    len = %d\n", len);

			while (i < len) {
				int l = min(len - i, 16);
				hex_dump_to_buffer(p + i, l, 16, 1, printbuf,
						   sizeof(printbuf), false);
				seq_printf(s, "      : %s\n", printbuf);
				i += l;
			}
		}
		seq_printf(s, "}\n");
	} else {
		seq_printf(s, "TxDesc index (%d) >= size (%d)\n",
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
static int wil_bf_debugfs_show(struct seq_file *s, void *data)
{
	struct wil6210_priv *wil = s->private;
	seq_printf(s,
		   "TSF : 0x%016llx\n"
		   "TxMCS : %d\n"
		   "Sectors(rx:tx) my %2d:%2d peer %2d:%2d\n",
		   wil->stats.tsf, wil->stats.bf_mcs,
		   wil->stats.my_rx_sector, wil->stats.my_tx_sector,
		   wil->stats.peer_rx_sector, wil->stats.peer_tx_sector);
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
		seq_printf(s, "Failed\n");
		return 0;
	}

	print_temp(s, "MAC temperature   :", t_m);
	print_temp(s, "Radio temperature :", t_r);

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

/*----------------*/
int wil6210_debugfs_init(struct wil6210_priv *wil)
{
	struct dentry *dbg = wil->debug = debugfs_create_dir(WIL_NAME,
			wil_to_wiphy(wil)->debugfsdir);

	if (IS_ERR_OR_NULL(dbg))
		return -ENODEV;

	debugfs_create_file("mbox", S_IRUGO, dbg, wil, &fops_mbox);
	debugfs_create_file("vrings", S_IRUGO, dbg, wil, &fops_vring);
	debugfs_create_file("txdesc", S_IRUGO, dbg, wil, &fops_txdesc);
	debugfs_create_u32("txdesc_index", S_IRUGO | S_IWUSR, dbg,
			   &dbg_txdesc_index);
	debugfs_create_file("bf", S_IRUGO, dbg, wil, &fops_bf);
	debugfs_create_file("ssid", S_IRUGO | S_IWUSR, dbg, wil, &fops_ssid);
	debugfs_create_u32("secure_pcp", S_IRUGO | S_IWUSR, dbg,
			   &wil->secure_pcp);

	wil6210_debugfs_create_ISR(wil, "USER_ICR", dbg,
				   HOSTADDR(RGF_USER_USER_ICR));
	wil6210_debugfs_create_ISR(wil, "DMA_EP_TX_ICR", dbg,
				   HOSTADDR(RGF_DMA_EP_TX_ICR));
	wil6210_debugfs_create_ISR(wil, "DMA_EP_RX_ICR", dbg,
				   HOSTADDR(RGF_DMA_EP_RX_ICR));
	wil6210_debugfs_create_ISR(wil, "DMA_EP_MISC_ICR", dbg,
				   HOSTADDR(RGF_DMA_EP_MISC_ICR));
	wil6210_debugfs_create_pseudo_ISR(wil, dbg);
	wil6210_debugfs_create_ITR_CNT(wil, dbg);

	debugfs_create_u32("mem_addr", S_IRUGO | S_IWUSR, dbg, &mem_addr);
	debugfs_create_file("mem_val", S_IRUGO, dbg, wil, &fops_memread);

	debugfs_create_file("reset", S_IWUSR, dbg, wil, &fops_reset);
	debugfs_create_file("temp", S_IRUGO, dbg, wil, &fops_temp);

	wil->rgf_blob.data = (void * __force)wil->csr + 0;
	wil->rgf_blob.size = 0xa000;
	wil_debugfs_create_ioblob("blob_rgf", S_IRUGO, dbg, &wil->rgf_blob);

	wil->fw_code_blob.data = (void * __force)wil->csr + 0x40000;
	wil->fw_code_blob.size = 0x40000;
	wil_debugfs_create_ioblob("blob_fw_code", S_IRUGO, dbg,
				  &wil->fw_code_blob);

	wil->fw_data_blob.data = (void * __force)wil->csr + 0x80000;
	wil->fw_data_blob.size = 0x8000;
	wil_debugfs_create_ioblob("blob_fw_data", S_IRUGO, dbg,
				  &wil->fw_data_blob);

	wil->fw_peri_blob.data = (void * __force)wil->csr + 0x88000;
	wil->fw_peri_blob.size = 0x18000;
	wil_debugfs_create_ioblob("blob_fw_peri", S_IRUGO, dbg,
				  &wil->fw_peri_blob);

	wil->uc_code_blob.data = (void * __force)wil->csr + 0xa0000;
	wil->uc_code_blob.size = 0x10000;
	wil_debugfs_create_ioblob("blob_uc_code", S_IRUGO, dbg,
				  &wil->uc_code_blob);

	wil->uc_data_blob.data = (void * __force)wil->csr + 0xb0000;
	wil->uc_data_blob.size = 0x4000;
	wil_debugfs_create_ioblob("blob_uc_data", S_IRUGO, dbg,
				  &wil->uc_data_blob);

	return 0;
}

void wil6210_debugfs_remove(struct wil6210_priv *wil)
{
	debugfs_remove_recursive(wil->debug);
	wil->debug = NULL;
}
