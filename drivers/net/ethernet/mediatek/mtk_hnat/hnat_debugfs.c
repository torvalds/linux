/*   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; version 2 of the License
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   Copyright (C) 2014-2016 Sean Wang <sean.wang@mediatek.com>
 *   Copyright (C) 2016-2017 John Crispin <blogic@openwrt.org>
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>

#include "hnat.h"

static const char *entry_state[] = {
	"INVALID",
	"UNBIND",
	"BIND",
	"FIN"
};

static const char *packet_type[] = {
	"IPV4_HNAPT",
	"IPV4_HNAT",
	"IPV6_1T_ROUTE",
	"IPV4_DSLITE",
	"IPV6_3T_ROUTE",
	"IPV6_5T_ROUTE",
	"IPV6_6RD",
};

static int hnat_debug_show(struct seq_file *m, void *private)
{
	struct hnat_priv *h = host;
	struct foe_entry *entry, *end;

	entry = h->foe_table_cpu;
	end = h->foe_table_cpu + FOE_4TB_SIZ;
	while (entry < end) {
		if (!entry->bfib1.state) {
			entry++;
			continue;
		}

		if (IS_IPV4_HNAPT(entry)) {
			__be32 saddr = htonl(entry->ipv4_hnapt.sip);
			__be32 daddr = htonl(entry->ipv4_hnapt.dip);
			__be32 nsaddr = htonl(entry->ipv4_hnapt.new_sip);
			__be32 ndaddr = htonl(entry->ipv4_hnapt.new_dip);
			unsigned char h_dest[ETH_ALEN];
			unsigned char h_source[ETH_ALEN];

			*((u32*) h_source) = swab32(entry->ipv4_hnapt.smac_hi);
			*((u16*) &h_source[4]) = swab16(entry->ipv4_hnapt.smac_lo);
			*((u32*) h_dest) = swab32(entry->ipv4_hnapt.dmac_hi);
			*((u16*) &h_dest[4]) = swab16(entry->ipv4_hnapt.dmac_lo);
			seq_printf(m,
				   "(%p)0x%05x|state=%s|type=%s|%pI4:%d->%pI4:%d=>%pI4:%d->%pI4:%d|%pM=>%pM|etype=0x%04x|info1=0x%x|info2=0x%x|vlan1=%d|vlan2=%d\n",
				   (void *)h->foe_table_dev + ((void *)(entry) - (void *)h->foe_table_cpu),
				   ei(entry, end), es(entry), pt(entry),
				   &saddr, entry->ipv4_hnapt.sport,
				   &daddr, entry->ipv4_hnapt.dport,
				   &nsaddr, entry->ipv4_hnapt.new_sport,
				   &ndaddr, entry->ipv4_hnapt.new_dport, h_source,
				   h_dest, ntohs(entry->ipv4_hnapt.etype),
				   entry->ipv4_hnapt.info_blk1,
				   entry->ipv4_hnapt.info_blk2,
				   entry->ipv4_hnapt.vlan1,
				   entry->ipv4_hnapt.vlan2);
		} else
			seq_printf(m, "0x%05x state=%s\n",
				   ei(entry, end), es(entry));
		entry++;
	}

	return 0;
}

static int hnat_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, hnat_debug_show, file->private_data);
}

static const struct file_operations hnat_debug_fops = {
	.open = hnat_debug_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

#define QDMA_TX_SCH_TX		0x1a14

static ssize_t hnat_sched_show(struct file *file, char __user *user_buf,
			       size_t count, loff_t *ppos)
{
	int id = (int) file->private_data;
	struct hnat_priv *h = host;
	u32 reg = readl(h->fe_base + QDMA_TX_SCH_TX);
	int enable;
	int max_rate;
	char *buf;
	unsigned int len = 0, buf_len = 1500;
	ssize_t ret_cnt;

	buf = kzalloc(buf_len, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;


	if (id)
		reg >>= 16;
	reg &= 0xffff;
	enable = !! (reg & BIT(11));
	max_rate = ((reg >> 4) & 0x7f);
	reg &= 0xf;
	while (reg--)
		max_rate *= 10;

	len += scnprintf(buf + len, buf_len - len,
			 "EN\tMAX\n%d\t%d\n", enable, max_rate);

	if (len > buf_len)
		len = buf_len;

	ret_cnt = simple_read_from_buffer(user_buf, count, ppos, buf, len);

	kfree(buf);
	return ret_cnt;
}

static ssize_t hnat_sched_write(struct file *file,
				const char __user *buf, size_t length, loff_t *offset)
{
	int id = (int) file->private_data;
	struct hnat_priv *h = host;
	char line[64];
	int enable, rate, exp = 0, shift = 0;
	size_t size;
	u32 reg = readl(h->fe_base + QDMA_TX_SCH_TX);
	u32 val = 0;

	if (length > sizeof(line))
		return -EINVAL;

        if (copy_from_user(line, buf, length))
		return -EFAULT;

	sscanf(line, "%d %d", &enable, &rate);

	while (rate > 127) {
		rate /= 10;
		exp++;
	}

	if (enable)
		val |= BIT(11);
	val |= (rate & 0x7f) << 4;
	val |= exp & 0xf;
	if (id)
		shift = 16;
	reg &= ~(0xffff << shift);
	reg |= val << shift;
	writel(reg, h->fe_base + QDMA_TX_SCH_TX);

	size = strlen(line);
	*offset += size;

	return length;
}

static const struct file_operations hnat_sched_fops = {
	.open = simple_open,
	.read = hnat_sched_show,
	.write = hnat_sched_write,
	.llseek = default_llseek,
};

#define QTX_CFG(x)	(0x1800 + (x * 0x10))
#define QTX_SCH(x)	(0x1804 + (x * 0x10))

static ssize_t hnat_queue_show(struct file *file, char __user *user_buf,
			       size_t count, loff_t *ppos)
{
	struct hnat_priv *h = host;
	int id = (int) file->private_data;
	u32 reg = readl(h->fe_base + QTX_SCH(id));
	u32 cfg = readl(h->fe_base + QTX_CFG(id));
	int scheduler = !!(reg & BIT(31));
	int min_rate_en = !!(reg & BIT(27));
	int min_rate = (reg >> 20) & 0x7f;
	int min_rate_exp = (reg >> 16) & 0xf;
	int max_rate_en = !!(reg & BIT(11));
	int max_weight = (reg >> 12) & 0xf;
	int max_rate = (reg >> 4) & 0x7f;
	int max_rate_exp = reg & 0xf;
	char *buf;
	unsigned int len = 0, buf_len = 1500;
	ssize_t ret_cnt;

	buf = kzalloc(buf_len, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	while (min_rate_exp--)
		min_rate *= 10;

	while (max_rate_exp--)
		max_rate *= 10;

	len += scnprintf(buf + len, buf_len - len,
			"scheduler: %d\nhw resv: %d\nsw resv: %d\n",
			scheduler, (cfg >> 8) & 0xff, cfg & 0xff);
	len += scnprintf(buf + len, buf_len - len,
			"\tEN\tRATE\t\tWEIGHT\n");
	len += scnprintf(buf + len, buf_len - len,
			"max\t%d\t%8d\t%d\n", max_rate_en, max_rate, max_weight);
	len += scnprintf(buf + len, buf_len - len,
			"min\t%d\t%8d\t-\n", min_rate_en, min_rate);

	if (len > buf_len)
		len = buf_len;

	ret_cnt = simple_read_from_buffer(user_buf, count, ppos, buf, len);

	kfree(buf);
	return ret_cnt;
}

static ssize_t hnat_queue_write(struct file *file,
				const char __user *buf, size_t length, loff_t *offset)
{
	int id = (int) file->private_data;
	struct hnat_priv *h = host;
	char line[64];
	int max_enable, max_rate, max_exp = 0;
	int min_enable, min_rate, min_exp = 0;
	int weight;
	int resv;
	int scheduler;
	size_t size;
	u32 reg = readl(h->fe_base + QTX_SCH(id));

	if (length > sizeof(line))
		return -EINVAL;

        if (copy_from_user(line, buf, length))
		return -EFAULT;

	sscanf(line, "%d %d %d %d %d %d %d", &scheduler, &min_enable, &min_rate, &max_enable, &max_rate, &weight, &resv);

	while (max_rate > 127) {
		max_rate /= 10;
		max_exp++;
	}

	while (min_rate > 127) {
		min_rate /= 10;
		min_exp++;
	}

	reg &= 0x70000000;
	if (scheduler)
		reg |= BIT(31);
	if (min_enable)
		reg |= BIT(27);
	reg |= (min_rate & 0x7f) << 20;
	reg |= (min_exp & 0xf) << 16;
	if (max_enable)
		reg |= BIT(11);
	reg |= (weight & 0xf) << 12;
	reg |= (max_rate & 0x7f) << 4;
	reg |= max_exp & 0xf;
	writel(reg, h->fe_base + QTX_SCH(id));

	resv &= 0xff;
	reg = readl(h->fe_base + QTX_CFG(id));
	reg &= 0xffff0000;
	reg |= (resv << 8) | resv;
	writel(reg, h->fe_base + QTX_CFG(id));

	size = strlen(line);
	*offset += size;

	return length;
}

static const struct file_operations hnat_queue_fops = {
	.open = simple_open,
	.read = hnat_queue_show,
	.write = hnat_queue_write,
	.llseek = default_llseek,
};

static void hnat_ac_timer_handle(unsigned long priv)
{
	struct hnat_priv *h = (struct hnat_priv*) priv;
	int i;

	for (i = 0; i < HNAT_COUNTER_MAX; i++) {
		u32 b_hi, b_lo;
		u64 b;

		b_lo = readl(h->fe_base + HNAT_AC_BYTE_LO(i));
		b_hi = readl(h->fe_base + HNAT_AC_BYTE_HI(i));
		b = b_hi;
		b <<= 32;
		b += b_lo;
		h->acct[i].bytes += b;
		h->acct[i].packets += readl(h->fe_base + HNAT_AC_PACKET(i));
	}

	mod_timer(&h->ac_timer, jiffies + HNAT_AC_TIMER_INTERVAL);
}

static ssize_t hnat_counter_show(struct file *file, char __user *user_buf,
				 size_t count, loff_t *ppos)
{
	struct hnat_priv *h = host;
	int id = (int) file->private_data;
	char *buf;
	unsigned int len = 0, buf_len = 1500;
	ssize_t ret_cnt;
	int id2 = id + (HNAT_COUNTER_MAX / 2);

	buf = kzalloc(buf_len, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	len += scnprintf(buf + len, buf_len - len,
			 "tx pkts : %llu\ntx bytes: %llu\nrx pktks : %llu\nrx bytes : %llu\n",
			 h->acct[id].packets, h->acct[id].bytes,
			 h->acct[id2].packets, h->acct[id2].bytes);

	if (len > buf_len)
		len = buf_len;

	ret_cnt = simple_read_from_buffer(user_buf, count, ppos, buf, len);

	kfree(buf);
	return ret_cnt;
}

static const struct file_operations hnat_counter_fops = {
	.open = simple_open,
	.read = hnat_counter_show,
	.llseek = default_llseek,
};

#define dump_register(nm)				\
{							\
	.name	= __stringify(nm),			\
	.offset	= PPE_ ##nm ,	\
}

static const struct debugfs_reg32 hnat_regs[] = {
	dump_register(GLO_CFG),
	dump_register(FLOW_CFG),
	dump_register(IP_PROT_CHK),
	dump_register(IP_PROT_0),
	dump_register(IP_PROT_1),
	dump_register(IP_PROT_2),
	dump_register(IP_PROT_3),
	dump_register(TB_CFG),
	dump_register(TB_BASE),
	dump_register(TB_USED),
	dump_register(BNDR),
	dump_register(BIND_LMT_0),
	dump_register(BIND_LMT_1),
	dump_register(KA),
	dump_register(UNB_AGE),
	dump_register(BND_AGE_0),
	dump_register(BND_AGE_1),
	dump_register(HASH_SEED),
	dump_register(DFT_CPORT),
	dump_register(MCAST_PPSE),
	dump_register(MCAST_L_0),
	dump_register(MCAST_H_0),
	dump_register(MCAST_L_1),
	dump_register(MCAST_H_1),
	dump_register(MCAST_L_2),
	dump_register(MCAST_H_2),
	dump_register(MCAST_L_3),
	dump_register(MCAST_H_3),
	dump_register(MCAST_L_4),
	dump_register(MCAST_H_4),
	dump_register(MCAST_L_5),
	dump_register(MCAST_H_5),
	dump_register(MCAST_L_6),
	dump_register(MCAST_H_6),
	dump_register(MCAST_L_7),
	dump_register(MCAST_H_7),
	dump_register(MCAST_L_8),
	dump_register(MCAST_H_8),
	dump_register(MCAST_L_9),
	dump_register(MCAST_H_9),
	dump_register(MCAST_L_A),
	dump_register(MCAST_H_A),
	dump_register(MCAST_L_B),
	dump_register(MCAST_H_B),
	dump_register(MCAST_L_C),
	dump_register(MCAST_H_C),
	dump_register(MCAST_L_D),
	dump_register(MCAST_H_D),
	dump_register(MCAST_L_E),
	dump_register(MCAST_H_E),
	dump_register(MCAST_L_F),
	dump_register(MCAST_H_F),
	dump_register(MTU_DRP),
	dump_register(MTU_VLYR_0),
	dump_register(MTU_VLYR_1),
	dump_register(MTU_VLYR_2),
	dump_register(VPM_TPID),
	dump_register(VPM_TPID),
	dump_register(CAH_CTRL),
	dump_register(CAH_TAG_SRH),
	dump_register(CAH_LINE_RW),
	dump_register(CAH_WDATA),
	dump_register(CAH_RDATA),
};

int __init hnat_init_debugfs(struct hnat_priv *h)
{
	int ret = 0;
	struct dentry *root;
	struct dentry *file;
	int i;
	char name[16];

	root = debugfs_create_dir("hnat", NULL);
	if (!root) {
		dev_err(h->dev, "%s:err at %d\n", __func__, __LINE__);
		ret = -ENOMEM;
		goto err0;
	}
	h->root = root;
	h->regset = kzalloc(sizeof(*h->regset), GFP_KERNEL);
	if (!h->regset) {
		dev_err(h->dev, "%s:err at %d\n", __func__, __LINE__);
		ret = -ENOMEM;
		goto err1;
	}
	h->regset->regs = hnat_regs;
	h->regset->nregs = ARRAY_SIZE(hnat_regs);
	h->regset->base = h->ppe_base;

	file = debugfs_create_regset32("regdump", S_IRUGO, root, h->regset);
	if (!file) {
		dev_err(h->dev, "%s:err at %d\n", __func__, __LINE__);
		ret = -ENOMEM;
		goto err1;
	}
	debugfs_create_file("all_entry", S_IRUGO, root, h, &hnat_debug_fops);
	for (i = 0; i < HNAT_COUNTER_MAX / 2; i++) {
		snprintf(name, sizeof(name), "counter%d", i);
		debugfs_create_file(name, S_IRUGO, root, (void *)i, &hnat_counter_fops);
	}

	for (i = 0; i < 2; i++) {
		snprintf(name, sizeof(name), "scheduler%d", i);
		debugfs_create_file(name, S_IRUGO, root, (void *)i, &hnat_sched_fops);
	}

	for (i = 0; i < 16; i++) {
		snprintf(name, sizeof(name), "queue%d", i);
		debugfs_create_file(name, S_IRUGO, root, (void *)i, &hnat_queue_fops);
	}

	setup_timer(&h->ac_timer, hnat_ac_timer_handle, (unsigned long) h);
	mod_timer(&h->ac_timer, jiffies + HNAT_AC_TIMER_INTERVAL);

	return 0;

 err1:
	debugfs_remove_recursive(root);
 err0:
	return ret;
}

void hnat_deinit_debugfs(struct hnat_priv *h)
{
	del_timer(&h->ac_timer);
	debugfs_remove_recursive(h->root);
	h->root = NULL;
}
