// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2018-2019  Realtek Corporation
 */

#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include "main.h"
#include "sec.h"
#include "fw.h"
#include "debug.h"
#include "phy.h"

#ifdef CONFIG_RTW88_DEBUGFS

struct rtw_debugfs_priv {
	struct rtw_dev *rtwdev;
	int (*cb_read)(struct seq_file *m, void *v);
	ssize_t (*cb_write)(struct file *filp, const char __user *buffer,
			    size_t count, loff_t *loff);
	union {
		u32 cb_data;
		u8 *buf;
		struct {
			u32 page_offset;
			u32 page_num;
		} rsvd_page;
		struct {
			u8 rf_path;
			u32 rf_addr;
			u32 rf_mask;
		};
		struct {
			u32 addr;
			u32 len;
		} read_reg;
	};
};

static int rtw_debugfs_single_show(struct seq_file *m, void *v)
{
	struct rtw_debugfs_priv *debugfs_priv = m->private;

	return debugfs_priv->cb_read(m, v);
}

static ssize_t rtw_debugfs_common_write(struct file *filp,
					const char __user *buffer,
					size_t count, loff_t *loff)
{
	struct rtw_debugfs_priv *debugfs_priv = filp->private_data;

	return debugfs_priv->cb_write(filp, buffer, count, loff);
}

static ssize_t rtw_debugfs_single_write(struct file *filp,
					const char __user *buffer,
					size_t count, loff_t *loff)
{
	struct seq_file *seqpriv = (struct seq_file *)filp->private_data;
	struct rtw_debugfs_priv *debugfs_priv = seqpriv->private;

	return debugfs_priv->cb_write(filp, buffer, count, loff);
}

static int rtw_debugfs_single_open_rw(struct inode *inode, struct file *filp)
{
	return single_open(filp, rtw_debugfs_single_show, inode->i_private);
}

static int rtw_debugfs_close(struct inode *inode, struct file *filp)
{
	return 0;
}

static const struct file_operations file_ops_single_r = {
	.owner = THIS_MODULE,
	.open = rtw_debugfs_single_open_rw,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations file_ops_single_rw = {
	.owner = THIS_MODULE,
	.open = rtw_debugfs_single_open_rw,
	.release = single_release,
	.read = seq_read,
	.llseek = seq_lseek,
	.write = rtw_debugfs_single_write,
};

static const struct file_operations file_ops_common_write = {
	.owner = THIS_MODULE,
	.write = rtw_debugfs_common_write,
	.open = simple_open,
	.release = rtw_debugfs_close,
};

static int rtw_debugfs_get_read_reg(struct seq_file *m, void *v)
{
	struct rtw_debugfs_priv *debugfs_priv = m->private;
	struct rtw_dev *rtwdev = debugfs_priv->rtwdev;
	u32 val, len, addr;

	len = debugfs_priv->read_reg.len;
	addr = debugfs_priv->read_reg.addr;
	switch (len) {
	case 1:
		val = rtw_read8(rtwdev, addr);
		seq_printf(m, "reg 0x%03x: 0x%02x\n", addr, val);
		break;
	case 2:
		val = rtw_read16(rtwdev, addr);
		seq_printf(m, "reg 0x%03x: 0x%04x\n", addr, val);
		break;
	case 4:
		val = rtw_read32(rtwdev, addr);
		seq_printf(m, "reg 0x%03x: 0x%08x\n", addr, val);
		break;
	}
	return 0;
}

static int rtw_debugfs_get_rf_read(struct seq_file *m, void *v)
{
	struct rtw_debugfs_priv *debugfs_priv = m->private;
	struct rtw_dev *rtwdev = debugfs_priv->rtwdev;
	u32 val, addr, mask;
	u8 path;

	path = debugfs_priv->rf_path;
	addr = debugfs_priv->rf_addr;
	mask = debugfs_priv->rf_mask;

	val = rtw_read_rf(rtwdev, path, addr, mask);

	seq_printf(m, "rf_read path:%d addr:0x%08x mask:0x%08x val=0x%08x\n",
		   path, addr, mask, val);

	return 0;
}

static int rtw_debugfs_copy_from_user(char tmp[], int size,
				      const char __user *buffer, size_t count,
				      int num)
{
	int tmp_len;

	if (count < num)
		return -EFAULT;

	tmp_len = (count > size - 1 ? size - 1 : count);

	if (!buffer || copy_from_user(tmp, buffer, tmp_len))
		return count;

	tmp[tmp_len] = '\0';

	return 0;
}

static ssize_t rtw_debugfs_set_read_reg(struct file *filp,
					const char __user *buffer,
					size_t count, loff_t *loff)
{
	struct seq_file *seqpriv = (struct seq_file *)filp->private_data;
	struct rtw_debugfs_priv *debugfs_priv = seqpriv->private;
	struct rtw_dev *rtwdev = debugfs_priv->rtwdev;
	char tmp[32 + 1];
	u32 addr, len;
	int num;

	rtw_debugfs_copy_from_user(tmp, sizeof(tmp), buffer, count, 2);

	num = sscanf(tmp, "%x %x", &addr, &len);

	if (num !=  2)
		return count;

	if (len != 1 && len != 2 && len != 4) {
		rtw_warn(rtwdev, "read reg setting wrong len\n");
		return -EINVAL;
	}
	debugfs_priv->read_reg.addr = addr;
	debugfs_priv->read_reg.len = len;

	return count;
}

static int rtw_debugfs_get_dump_cam(struct seq_file *m, void *v)
{
	struct rtw_debugfs_priv *debugfs_priv = m->private;
	struct rtw_dev *rtwdev = debugfs_priv->rtwdev;
	u32 val, command;
	u32 hw_key_idx = debugfs_priv->cb_data << RTW_SEC_CAM_ENTRY_SHIFT;
	u32 read_cmd = RTW_SEC_CMD_POLLING;
	int i;

	seq_printf(m, "cam entry%d\n", debugfs_priv->cb_data);
	seq_puts(m, "0x0      0x1      0x2     0x3     ");
	seq_puts(m, "0x4     0x5\n");
	mutex_lock(&rtwdev->mutex);
	for (i = 0; i <= 5; i++) {
		command = read_cmd | (hw_key_idx + i);
		rtw_write32(rtwdev, RTW_SEC_CMD_REG, command);
		val = rtw_read32(rtwdev, RTW_SEC_READ_REG);
		seq_printf(m, "%8.8x", val);
		if (i < 2)
			seq_puts(m, " ");
	}
	seq_puts(m, "\n");
	mutex_unlock(&rtwdev->mutex);
	return 0;
}

static int rtw_debugfs_get_rsvd_page(struct seq_file *m, void *v)
{
	struct rtw_debugfs_priv *debugfs_priv = m->private;
	struct rtw_dev *rtwdev = debugfs_priv->rtwdev;
	u8 page_size = rtwdev->chip->page_size;
	u32 buf_size = debugfs_priv->rsvd_page.page_num * page_size;
	u32 offset = debugfs_priv->rsvd_page.page_offset * page_size;
	u8 *buf;
	int i;
	int ret;

	buf = vzalloc(buf_size);
	if (!buf)
		return -ENOMEM;

	ret = rtw_dump_drv_rsvd_page(rtwdev, offset, buf_size, (u32 *)buf);
	if (ret) {
		rtw_err(rtwdev, "failed to dump rsvd page\n");
		vfree(buf);
		return ret;
	}

	for (i = 0 ; i < buf_size ; i += 8) {
		if (i % page_size == 0)
			seq_printf(m, "PAGE %d\n", (i + offset) / page_size);
		seq_printf(m, "%2.2x %2.2x %2.2x %2.2x %2.2x %2.2x %2.2x %2.2x\n",
			   *(buf + i), *(buf + i + 1),
			   *(buf + i + 2), *(buf + i + 3),
			   *(buf + i + 4), *(buf + i + 5),
			   *(buf + i + 6), *(buf + i + 7));
	}
	vfree(buf);

	return 0;
}

static ssize_t rtw_debugfs_set_rsvd_page(struct file *filp,
					 const char __user *buffer,
					 size_t count, loff_t *loff)
{
	struct seq_file *seqpriv = (struct seq_file *)filp->private_data;
	struct rtw_debugfs_priv *debugfs_priv = seqpriv->private;
	struct rtw_dev *rtwdev = debugfs_priv->rtwdev;
	char tmp[32 + 1];
	u32 offset, page_num;
	int num;

	rtw_debugfs_copy_from_user(tmp, sizeof(tmp), buffer, count, 2);

	num = sscanf(tmp, "%d %d", &offset, &page_num);

	if (num != 2) {
		rtw_warn(rtwdev, "invalid arguments\n");
		return num;
	}

	debugfs_priv->rsvd_page.page_offset = offset;
	debugfs_priv->rsvd_page.page_num = page_num;

	return count;
}

static ssize_t rtw_debugfs_set_single_input(struct file *filp,
					    const char __user *buffer,
					    size_t count, loff_t *loff)
{
	struct seq_file *seqpriv = (struct seq_file *)filp->private_data;
	struct rtw_debugfs_priv *debugfs_priv = seqpriv->private;
	struct rtw_dev *rtwdev = debugfs_priv->rtwdev;
	char tmp[32 + 1];
	u32 input;
	int num;

	rtw_debugfs_copy_from_user(tmp, sizeof(tmp), buffer, count, 1);

	num = kstrtoint(tmp, 0, &input);

	if (num) {
		rtw_warn(rtwdev, "kstrtoint failed\n");
		return num;
	}

	debugfs_priv->cb_data = input;

	return count;
}

static ssize_t rtw_debugfs_set_write_reg(struct file *filp,
					 const char __user *buffer,
					 size_t count, loff_t *loff)
{
	struct rtw_debugfs_priv *debugfs_priv = filp->private_data;
	struct rtw_dev *rtwdev = debugfs_priv->rtwdev;
	char tmp[32 + 1];
	u32 addr, val, len;
	int num;

	rtw_debugfs_copy_from_user(tmp, sizeof(tmp), buffer, count, 3);

	/* write BB/MAC register */
	num = sscanf(tmp, "%x %x %x", &addr, &val, &len);

	if (num !=  3)
		return count;

	switch (len) {
	case 1:
		rtw_dbg(rtwdev, RTW_DBG_DEBUGFS,
			"reg write8 0x%03x: 0x%08x\n", addr, val);
		rtw_write8(rtwdev, addr, (u8)val);
		break;
	case 2:
		rtw_dbg(rtwdev, RTW_DBG_DEBUGFS,
			"reg write16 0x%03x: 0x%08x\n", addr, val);
		rtw_write16(rtwdev, addr, (u16)val);
		break;
	case 4:
		rtw_dbg(rtwdev, RTW_DBG_DEBUGFS,
			"reg write32 0x%03x: 0x%08x\n", addr, val);
		rtw_write32(rtwdev, addr, (u32)val);
		break;
	default:
		rtw_dbg(rtwdev, RTW_DBG_DEBUGFS,
			"error write length = %d\n", len);
		break;
	}

	return count;
}

static ssize_t rtw_debugfs_set_rf_write(struct file *filp,
					const char __user *buffer,
					size_t count, loff_t *loff)
{
	struct rtw_debugfs_priv *debugfs_priv = filp->private_data;
	struct rtw_dev *rtwdev = debugfs_priv->rtwdev;
	char tmp[32 + 1];
	u32 path, addr, mask, val;
	int num;

	rtw_debugfs_copy_from_user(tmp, sizeof(tmp), buffer, count, 4);

	num = sscanf(tmp, "%x %x %x %x", &path, &addr, &mask, &val);

	if (num !=  4) {
		rtw_warn(rtwdev, "invalid args, [path] [addr] [mask] [val]\n");
		return count;
	}

	rtw_write_rf(rtwdev, path, addr, mask, val);
	rtw_dbg(rtwdev, RTW_DBG_DEBUGFS,
		"write_rf path:%d addr:0x%08x mask:0x%08x, val:0x%08x\n",
		path, addr, mask, val);

	return count;
}

static ssize_t rtw_debugfs_set_rf_read(struct file *filp,
				       const char __user *buffer,
				       size_t count, loff_t *loff)
{
	struct seq_file *seqpriv = (struct seq_file *)filp->private_data;
	struct rtw_debugfs_priv *debugfs_priv = seqpriv->private;
	struct rtw_dev *rtwdev = debugfs_priv->rtwdev;
	char tmp[32 + 1];
	u32 path, addr, mask;
	int num;

	rtw_debugfs_copy_from_user(tmp, sizeof(tmp), buffer, count, 3);

	num = sscanf(tmp, "%x %x %x", &path, &addr, &mask);

	if (num !=  3) {
		rtw_warn(rtwdev, "invalid args, [path] [addr] [mask] [val]\n");
		return count;
	}

	debugfs_priv->rf_path = path;
	debugfs_priv->rf_addr = addr;
	debugfs_priv->rf_mask = mask;

	return count;
}

static int rtw_debug_get_mac_page(struct seq_file *m, void *v)
{
	struct rtw_debugfs_priv *debugfs_priv = m->private;
	struct rtw_dev *rtwdev = debugfs_priv->rtwdev;
	u32 val;
	u32 page = debugfs_priv->cb_data;
	int i, n;
	int max = 0xff;

	val = rtw_read32(rtwdev, debugfs_priv->cb_data);
	for (n = 0; n <= max; ) {
		seq_printf(m, "\n%8.8x  ", n + page);
		for (i = 0; i < 4 && n <= max; i++, n += 4)
			seq_printf(m, "%8.8x    ",
				   rtw_read32(rtwdev, (page | n)));
	}
	seq_puts(m, "\n");
	return 0;
}

static int rtw_debug_get_bb_page(struct seq_file *m, void *v)
{
	struct rtw_debugfs_priv *debugfs_priv = m->private;
	struct rtw_dev *rtwdev = debugfs_priv->rtwdev;
	u32 val;
	u32 page = debugfs_priv->cb_data;
	int i, n;
	int max = 0xff;

	val = rtw_read32(rtwdev, debugfs_priv->cb_data);
	for (n = 0; n <= max; ) {
		seq_printf(m, "\n%8.8x  ", n + page);
		for (i = 0; i < 4 && n <= max; i++, n += 4)
			seq_printf(m, "%8.8x    ",
				   rtw_read32(rtwdev, (page | n)));
	}
	seq_puts(m, "\n");
	return 0;
}

static int rtw_debug_get_rf_dump(struct seq_file *m, void *v)
{
	struct rtw_debugfs_priv *debugfs_priv = m->private;
	struct rtw_dev *rtwdev = debugfs_priv->rtwdev;
	u32 addr, offset, data;
	u8 path;

	for (path = 0; path < rtwdev->hal.rf_path_num; path++) {
		seq_printf(m, "RF path:%d\n", path);
		for (addr = 0; addr < 0x100; addr += 4) {
			seq_printf(m, "%8.8x  ", addr);
			for (offset = 0; offset < 4; offset++) {
				data = rtw_read_rf(rtwdev, path, addr + offset,
						   0xffffffff);
				seq_printf(m, "%8.8x    ", data);
			}
			seq_puts(m, "\n");
		}
		seq_puts(m, "\n");
	}

	return 0;
}

static void rtw_print_cck_rate_txt(struct seq_file *m, u8 rate)
{
	static const char * const
	cck_rate[] = {"1M", "2M", "5.5M", "11M"};
	u8 idx = rate - DESC_RATE1M;

	seq_printf(m, " CCK_%-5s", cck_rate[idx]);
}

static void rtw_print_ofdm_rate_txt(struct seq_file *m, u8 rate)
{
	static const char * const
	ofdm_rate[] = {"6M", "9M", "12M", "18M", "24M", "36M", "48M", "54M"};
	u8 idx = rate - DESC_RATE6M;

	seq_printf(m, " OFDM_%-4s", ofdm_rate[idx]);
}

static void rtw_print_ht_rate_txt(struct seq_file *m, u8 rate)
{
	u8 mcs_n = rate - DESC_RATEMCS0;

	seq_printf(m, " MCS%-6u", mcs_n);
}

static void rtw_print_vht_rate_txt(struct seq_file *m, u8 rate)
{
	u8 idx = rate - DESC_RATEVHT1SS_MCS0;
	u8 n_ss, mcs_n;

	/* n spatial stream */
	n_ss = 1 + idx / 10;
	/* MCS n */
	mcs_n = idx % 10;
	seq_printf(m, " VHT%uSMCS%u", n_ss, mcs_n);
}

static int rtw_debugfs_get_tx_pwr_tbl(struct seq_file *m, void *v)
{
	struct rtw_debugfs_priv *debugfs_priv = m->private;
	struct rtw_dev *rtwdev = debugfs_priv->rtwdev;
	struct rtw_hal *hal = &rtwdev->hal;
	void (*print_rate)(struct seq_file *, u8) = NULL;
	u8 path, rate;
	struct rtw_power_params pwr_param = {0};
	u8 bw = hal->current_band_width;
	u8 ch = hal->current_channel;
	u8 regd = rtwdev->regd.txpwr_regd;

	seq_printf(m, "%-4s %-10s %-3s%6s %-4s %4s (%-4s %-4s)\n",
		   "path", "rate", "pwr", "", "base", "", "byr", "lmt");

	mutex_lock(&hal->tx_power_mutex);
	for (path = RF_PATH_A; path <= RF_PATH_B; path++) {
		/* there is no CCK rates used in 5G */
		if (hal->current_band_type == RTW_BAND_5G)
			rate = DESC_RATE6M;
		else
			rate = DESC_RATE1M;

		/* now, not support vht 3ss and vht 4ss*/
		for (; rate <= DESC_RATEVHT2SS_MCS9; rate++) {
			/* now, not support ht 3ss and ht 4ss*/
			if (rate > DESC_RATEMCS15 &&
			    rate < DESC_RATEVHT1SS_MCS0)
				continue;

			switch (rate) {
			case DESC_RATE1M...DESC_RATE11M:
				print_rate = rtw_print_cck_rate_txt;
				break;
			case DESC_RATE6M...DESC_RATE54M:
				print_rate = rtw_print_ofdm_rate_txt;
				break;
			case DESC_RATEMCS0...DESC_RATEMCS15:
				print_rate = rtw_print_ht_rate_txt;
				break;
			case DESC_RATEVHT1SS_MCS0...DESC_RATEVHT2SS_MCS9:
				print_rate = rtw_print_vht_rate_txt;
				break;
			default:
				print_rate = NULL;
				break;
			}

			rtw_get_tx_power_params(rtwdev, path, rate, bw,
						ch, regd, &pwr_param);

			seq_printf(m, "%4c ", path + 'A');
			if (print_rate)
				print_rate(m, rate);
			seq_printf(m, " %3u(0x%02x) %4u %4d (%4d %4d)\n",
				   hal->tx_pwr_tbl[path][rate],
				   hal->tx_pwr_tbl[path][rate],
				   pwr_param.pwr_base,
				   min_t(s8, pwr_param.pwr_offset,
					 pwr_param.pwr_limit),
				   pwr_param.pwr_offset, pwr_param.pwr_limit);
		}
	}

	mutex_unlock(&hal->tx_power_mutex);

	return 0;
}

#define rtw_debug_impl_mac(page, addr)				\
static struct rtw_debugfs_priv rtw_debug_priv_mac_ ##page = {	\
	.cb_read = rtw_debug_get_mac_page,			\
	.cb_data = addr,					\
}

rtw_debug_impl_mac(0, 0x0000);
rtw_debug_impl_mac(1, 0x0100);
rtw_debug_impl_mac(2, 0x0200);
rtw_debug_impl_mac(3, 0x0300);
rtw_debug_impl_mac(4, 0x0400);
rtw_debug_impl_mac(5, 0x0500);
rtw_debug_impl_mac(6, 0x0600);
rtw_debug_impl_mac(7, 0x0700);
rtw_debug_impl_mac(10, 0x1000);
rtw_debug_impl_mac(11, 0x1100);
rtw_debug_impl_mac(12, 0x1200);
rtw_debug_impl_mac(13, 0x1300);
rtw_debug_impl_mac(14, 0x1400);
rtw_debug_impl_mac(15, 0x1500);
rtw_debug_impl_mac(16, 0x1600);
rtw_debug_impl_mac(17, 0x1700);

#define rtw_debug_impl_bb(page, addr)			\
static struct rtw_debugfs_priv rtw_debug_priv_bb_ ##page = {	\
	.cb_read = rtw_debug_get_bb_page,			\
	.cb_data = addr,					\
}

rtw_debug_impl_bb(8, 0x0800);
rtw_debug_impl_bb(9, 0x0900);
rtw_debug_impl_bb(a, 0x0a00);
rtw_debug_impl_bb(b, 0x0b00);
rtw_debug_impl_bb(c, 0x0c00);
rtw_debug_impl_bb(d, 0x0d00);
rtw_debug_impl_bb(e, 0x0e00);
rtw_debug_impl_bb(f, 0x0f00);
rtw_debug_impl_bb(18, 0x1800);
rtw_debug_impl_bb(19, 0x1900);
rtw_debug_impl_bb(1a, 0x1a00);
rtw_debug_impl_bb(1b, 0x1b00);
rtw_debug_impl_bb(1c, 0x1c00);
rtw_debug_impl_bb(1d, 0x1d00);
rtw_debug_impl_bb(1e, 0x1e00);
rtw_debug_impl_bb(1f, 0x1f00);
rtw_debug_impl_bb(2c, 0x2c00);
rtw_debug_impl_bb(2d, 0x2d00);
rtw_debug_impl_bb(40, 0x4000);
rtw_debug_impl_bb(41, 0x4100);

static struct rtw_debugfs_priv rtw_debug_priv_rf_dump = {
	.cb_read = rtw_debug_get_rf_dump,
};

static struct rtw_debugfs_priv rtw_debug_priv_tx_pwr_tbl = {
	.cb_read = rtw_debugfs_get_tx_pwr_tbl,
};

static struct rtw_debugfs_priv rtw_debug_priv_write_reg = {
	.cb_write = rtw_debugfs_set_write_reg,
};

static struct rtw_debugfs_priv rtw_debug_priv_rf_write = {
	.cb_write = rtw_debugfs_set_rf_write,
};

static struct rtw_debugfs_priv rtw_debug_priv_rf_read = {
	.cb_write = rtw_debugfs_set_rf_read,
	.cb_read = rtw_debugfs_get_rf_read,
};

static struct rtw_debugfs_priv rtw_debug_priv_read_reg = {
	.cb_write = rtw_debugfs_set_read_reg,
	.cb_read = rtw_debugfs_get_read_reg,
};

static struct rtw_debugfs_priv rtw_debug_priv_dump_cam = {
	.cb_write = rtw_debugfs_set_single_input,
	.cb_read = rtw_debugfs_get_dump_cam,
};

static struct rtw_debugfs_priv rtw_debug_priv_rsvd_page = {
	.cb_write = rtw_debugfs_set_rsvd_page,
	.cb_read = rtw_debugfs_get_rsvd_page,
};

#define rtw_debugfs_add_core(name, mode, fopname, parent)		\
	do {								\
		rtw_debug_priv_ ##name.rtwdev = rtwdev;			\
		if (!debugfs_create_file(#name, mode,			\
					 parent, &rtw_debug_priv_ ##name,\
					 &file_ops_ ##fopname))		\
			pr_debug("Unable to initialize debugfs:%s\n",	\
			       #name);					\
	} while (0)

#define rtw_debugfs_add_w(name)						\
	rtw_debugfs_add_core(name, S_IFREG | 0222, common_write, debugfs_topdir)
#define rtw_debugfs_add_rw(name)					\
	rtw_debugfs_add_core(name, S_IFREG | 0666, single_rw, debugfs_topdir)
#define rtw_debugfs_add_r(name)						\
	rtw_debugfs_add_core(name, S_IFREG | 0444, single_r, debugfs_topdir)

void rtw_debugfs_init(struct rtw_dev *rtwdev)
{
	struct dentry *debugfs_topdir;

	debugfs_topdir = debugfs_create_dir("rtw88",
					    rtwdev->hw->wiphy->debugfsdir);
	rtw_debugfs_add_w(write_reg);
	rtw_debugfs_add_rw(read_reg);
	rtw_debugfs_add_w(rf_write);
	rtw_debugfs_add_rw(rf_read);
	rtw_debugfs_add_rw(dump_cam);
	rtw_debugfs_add_rw(rsvd_page);
	rtw_debugfs_add_r(mac_0);
	rtw_debugfs_add_r(mac_1);
	rtw_debugfs_add_r(mac_2);
	rtw_debugfs_add_r(mac_3);
	rtw_debugfs_add_r(mac_4);
	rtw_debugfs_add_r(mac_5);
	rtw_debugfs_add_r(mac_6);
	rtw_debugfs_add_r(mac_7);
	rtw_debugfs_add_r(bb_8);
	rtw_debugfs_add_r(bb_9);
	rtw_debugfs_add_r(bb_a);
	rtw_debugfs_add_r(bb_b);
	rtw_debugfs_add_r(bb_c);
	rtw_debugfs_add_r(bb_d);
	rtw_debugfs_add_r(bb_e);
	rtw_debugfs_add_r(bb_f);
	rtw_debugfs_add_r(mac_10);
	rtw_debugfs_add_r(mac_11);
	rtw_debugfs_add_r(mac_12);
	rtw_debugfs_add_r(mac_13);
	rtw_debugfs_add_r(mac_14);
	rtw_debugfs_add_r(mac_15);
	rtw_debugfs_add_r(mac_16);
	rtw_debugfs_add_r(mac_17);
	rtw_debugfs_add_r(bb_18);
	rtw_debugfs_add_r(bb_19);
	rtw_debugfs_add_r(bb_1a);
	rtw_debugfs_add_r(bb_1b);
	rtw_debugfs_add_r(bb_1c);
	rtw_debugfs_add_r(bb_1d);
	rtw_debugfs_add_r(bb_1e);
	rtw_debugfs_add_r(bb_1f);
	if (rtwdev->chip->id == RTW_CHIP_TYPE_8822C) {
		rtw_debugfs_add_r(bb_2c);
		rtw_debugfs_add_r(bb_2d);
		rtw_debugfs_add_r(bb_40);
		rtw_debugfs_add_r(bb_41);
	}
	rtw_debugfs_add_r(rf_dump);
	rtw_debugfs_add_r(tx_pwr_tbl);
}

#endif /* CONFIG_RTW88_DEBUGFS */

#ifdef CONFIG_RTW88_DEBUG

void __rtw_dbg(struct rtw_dev *rtwdev, enum rtw_debug_mask mask,
	       const char *fmt, ...)
{
	struct va_format vaf = {
		.fmt = fmt,
	};
	va_list args;

	va_start(args, fmt);
	vaf.va = &args;

	if (rtw_debug_mask & mask)
		dev_printk(KERN_DEBUG, rtwdev->dev, "%pV", &vaf);

	va_end(args);
}
EXPORT_SYMBOL(__rtw_dbg);

#endif /* CONFIG_RTW88_DEBUG */
